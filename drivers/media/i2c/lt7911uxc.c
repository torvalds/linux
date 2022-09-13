// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 *
 * lt7911uxc type-c/DP to MIPI CSI-2 bridge driver.
 *
 * Author: Jianwei Fan <jianwei.fan@rock-chips.com>
 *
 * V0.0X01.0X00 first version.
 * V0.0X01.0X01 support DPHY 4K60.
 * V0.0X01.0X02 add CPHY support.
 * V0.0X01.0X03 add rk3588 dcphy param.
 * V0.0X01.0X04 add 5K60 support for CPHY.
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/hdmi.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/rk-camera-module.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/v4l2-dv-timings.h>
#include <linux/version.h>
#include <linux/videodev2.h>
#include <linux/workqueue.h>
#include <linux/compat.h>
#include <media/v4l2-controls_rockchip.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x04)

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-3)");

#define I2C_MAX_XFER_SIZE	128
#define POLL_INTERVAL_MS	1000

#define LT7911UXC_LINK_FREQ_HIGH	1250000000
#define LT7911UXC_LINK_FREQ_LOW		400000000
#define LT7911UXC_LINK_FREQ_700M	700000000
#define LT7911UXC_PIXEL_RATE		800000000

#define LT7911UXC_CHIPID	0x0119
#define CHIPID_REGH		0xe101
#define CHIPID_REGL		0xe100
#define I2C_EN_REG		0xe0ee
#define I2C_ENABLE		0x1
#define I2C_DISABLE		0x0

#define HTOTAL_H		0xe088
#define HTOTAL_L		0xe089
#define HACT_H			0xe08c
#define HACT_L			0xe08d

#define VTOTAL_H		0xe08a
#define VTOTAL_L		0xe08b
#define VACT_H			0xe08e
#define VACT_L			0xe08f

#define PCLK_H			0xe085
#define PCLK_M			0xe086
#define PCLK_L			0xe087

#define BYTE_PCLK_H		0xe092
#define BYTE_PCLK_M		0xe093
#define BYTE_PCLK_L		0xe094

#define AUDIO_FS_VALUE_H	0xe090
#define AUDIO_FS_VALUE_L	0xe091

//CPHY timing
#define CLK_ZERO_REG		0xf9a7
#define CLK_PRE_REG		0xf9a8
#define CLK_POST_REG		0xf9a9
#define HS_LPX_REG		0xf9a4
#define HS_PREP_REG		0xf9a5
#define HS_TRAIL		0xf9a6
#define HS_RQST_PRE_REG		0xf98a

#define STREAM_CTL		0xe0b0
#define ENABLE_STREAM		0x01
#define DISABLE_STREAM		0x00

#define LT7911UXC_NAME			"LT7911UXC"

static const s64 link_freq_menu_items[] = {
	LT7911UXC_LINK_FREQ_HIGH,
	LT7911UXC_LINK_FREQ_LOW,
	LT7911UXC_LINK_FREQ_700M,
};

struct lt7911uxc {
	struct v4l2_fwnode_bus_mipi_csi2 bus;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler hdl;
	struct i2c_client *i2c_client;
	struct mutex confctl_mutex;
	struct v4l2_ctrl *detect_tx_5v_ctrl;
	struct v4l2_ctrl *audio_sampling_rate_ctrl;
	struct v4l2_ctrl *audio_present_ctrl;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct delayed_work delayed_work_hotplug;
	struct delayed_work delayed_work_res_change;
	struct v4l2_dv_timings timings;
	struct clk *xvclk;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *plugin_det_gpio;
	struct gpio_desc *power_gpio;
	struct work_struct work_i2c_poll;
	struct timer_list timer;
	const char *module_facing;
	const char *module_name;
	const char *len_name;
	const struct lt7911uxc_mode *cur_mode;
	const struct lt7911uxc_mode *support_modes;
	u32 cfg_num;
	struct v4l2_fwnode_endpoint bus_cfg;
	bool nosignal;
	bool enable_hdcp;
	bool is_audio_present;
	bool power_on;
	int plugin_irq;
	u32 mbus_fmt_code;
	u32 module_index;
	u32 audio_sampling_rate;
};

static const struct v4l2_dv_timings_cap lt7911uxc_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	.reserved = { 0 },
	V4L2_INIT_BT_TIMINGS(1, 10000, 1, 10000, 0, 800000000,
			V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
			V4L2_DV_BT_STD_GTF | V4L2_DV_BT_STD_CVT,
			V4L2_DV_BT_CAP_PROGRESSIVE | V4L2_DV_BT_CAP_INTERLACED |
			V4L2_DV_BT_CAP_REDUCED_BLANKING |
			V4L2_DV_BT_CAP_CUSTOM)
};

struct lt7911uxc_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	u32 mipi_freq_idx;
};

static struct rkmodule_csi_dphy_param rk3588_dcphy_param = {
	.vendor = PHY_VENDOR_SAMSUNG,
	.lp_vol_ref = 3,
	.lp_hys_sw = {0, 0, 0, 0},
	.lp_escclk_pol_sel = {1, 0, 1, 0},
	.skew_data_cal_clk = {0, 0, 0, 0},
	.clk_hs_term_sel = 2,
	.data_hs_term_sel = {2, 2, 2, 2},
	.reserved = {0},
};

static const struct lt7911uxc_mode supported_modes_dphy[] = {
	{
		.width = 3840,
		.height = 2160,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 4400,
		.vts_def = 2250,
		.mipi_freq_idx = 0,
	}, {
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 2200,
		.vts_def = 1125,
		.mipi_freq_idx = 0,
	}, {
		.width = 1280,
		.height = 720,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 1650,
		.vts_def = 750,
		.mipi_freq_idx = 0,
	}, {
		.width = 720,
		.height = 576,
		.max_fps = {
			.numerator = 10000,
			.denominator = 500000,
		},
		.hts_def = 864,
		.vts_def = 625,
		.mipi_freq_idx = 1,
	}, {
		.width = 720,
		.height = 480,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 858,
		.vts_def = 525,
		.mipi_freq_idx = 1,
	},
};

static const struct lt7911uxc_mode supported_modes_cphy[] = {
	{
		.width = 5120,
		.height = 2160,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 5500,
		.vts_def = 2250,
		.mipi_freq_idx = 2,
	}, {
		.width = 3840,
		.height = 2160,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 4400,
		.vts_def = 2250,
		.mipi_freq_idx = 1,
	}, {
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 2200,
		.vts_def = 1125,
		.mipi_freq_idx = 1,
	}, {
		.width = 1280,
		.height = 720,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 1650,
		.vts_def = 750,
		.mipi_freq_idx = 1,
	}, {
		.width = 720,
		.height = 576,
		.max_fps = {
			.numerator = 10000,
			.denominator = 500000,
		},
		.hts_def = 864,
		.vts_def = 625,
		.mipi_freq_idx = 1,
	}, {
		.width = 720,
		.height = 480,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 858,
		.vts_def = 525,
		.mipi_freq_idx = 1,
	},
};

static void lt7911uxc_format_change(struct v4l2_subdev *sd);
static int lt7911uxc_s_ctrl_detect_tx_5v(struct v4l2_subdev *sd);
static int lt7911uxc_s_dv_timings(struct v4l2_subdev *sd,
				struct v4l2_dv_timings *timings);

static inline struct lt7911uxc *to_lt7911uxc(struct v4l2_subdev *sd)
{
	return container_of(sd, struct lt7911uxc, sd);
}

static void i2c_rd(struct v4l2_subdev *sd, u16 reg, u8 *values, u32 n)
{
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);
	struct i2c_client *client = lt7911uxc->i2c_client;
	int err;
	u8 buf[2] = { 0xFF, reg >> 8};
	u8 reg_addr = reg & 0xFF;
	struct i2c_msg msgs[3];

	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = buf;

	msgs[1].addr = client->addr;
	msgs[1].flags = 0;
	msgs[1].len = 1;
	msgs[1].buf = &reg_addr;

	msgs[2].addr = client->addr;
	msgs[2].flags = I2C_M_RD;
	msgs[2].len = n;
	msgs[2].buf = values;

	err = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (err != ARRAY_SIZE(msgs)) {
		v4l2_err(sd, "%s: reading register 0x%x from 0x%x failed\n",
				__func__, reg, client->addr);
	}

	if (!debug)
		return;

	switch (n) {
	case 1:
		v4l2_info(sd, "I2C read 0x%04x = 0x%02x\n",
			reg, values[0]);
		break;
	case 2:
		v4l2_info(sd, "I2C read 0x%04x = 0x%02x%02x\n",
			reg, values[1], values[0]);
		break;
	case 4:
		v4l2_info(sd, "I2C read 0x%04x = 0x%02x%02x%02x%02x\n",
			reg, values[3], values[2], values[1], values[0]);
		break;
	default:
		v4l2_info(sd, "I2C read %d bytes from address 0x%04x\n",
			n, reg);
	}
}

static void i2c_wr(struct v4l2_subdev *sd, u16 reg, u8 *values, u32 n)
{
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);
	struct i2c_client *client = lt7911uxc->i2c_client;
	int err, i;
	struct i2c_msg msgs[2];
	u8 data[I2C_MAX_XFER_SIZE];
	u8 buf[2] = { 0xFF, reg >> 8};

	if ((1 + n) > I2C_MAX_XFER_SIZE) {
		n = I2C_MAX_XFER_SIZE - 1;
		v4l2_warn(sd, "i2c wr reg=%04x: len=%d is too big!\n",
			  reg, 1 + n);
	}

	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = buf;

	msgs[1].addr = client->addr;
	msgs[1].flags = 0;
	msgs[1].len = 1 + n;
	msgs[1].buf = data;

	data[0] = reg & 0xff;
	for (i = 0; i < n; i++)
		data[1 + i] = values[i];

	err = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (err < 0) {
		v4l2_err(sd, "%s: writing register 0x%x from 0x%x failed\n",
				__func__, reg, client->addr);
		return;
	}

	if (!debug)
		return;

	switch (n) {
	case 1:
		v4l2_info(sd, "I2C write 0x%04x = 0x%02x\n",
				reg, data[1]);
		break;
	case 2:
		v4l2_info(sd, "I2C write 0x%04x = 0x%02x%02x\n",
				reg, data[2], data[1]);
		break;
	case 4:
		v4l2_info(sd, "I2C write 0x%04x = 0x%02x%02x%02x%02x\n",
				reg, data[4], data[3], data[2], data[1]);
		break;
	default:
		v4l2_info(sd, "I2C write %d bytes from address 0x%04x\n",
				n, reg);
	}
}

static u8 i2c_rd8(struct v4l2_subdev *sd, u16 reg)
{
	u32 val;

	i2c_rd(sd, reg, (u8 __force *)&val, 1);
	return val;
}

static void i2c_wr8(struct v4l2_subdev *sd, u16 reg, u8 val)
{
	i2c_wr(sd, reg, &val, 1);
}

static void lt7911uxc_i2c_enable(struct v4l2_subdev *sd)
{
	i2c_wr8(sd, I2C_EN_REG, I2C_ENABLE);
}

static void lt7911uxc_i2c_disable(struct v4l2_subdev *sd)
{
	i2c_wr8(sd, I2C_EN_REG, I2C_DISABLE);
}

static inline bool tx_5v_power_present(struct v4l2_subdev *sd)
{
	bool ret;
	int val, i, cnt;
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);

	/* if not use plugin det gpio */
	if (!lt7911uxc->plugin_det_gpio)
		return true;

	cnt = 0;
	for (i = 0; i < 5; i++) {
		val = gpiod_get_value(lt7911uxc->plugin_det_gpio);
		if (val > 0)
			cnt++;
		usleep_range(500, 600);
	}

	ret = (cnt >= 3) ? true : false;
	v4l2_dbg(1, debug, sd, "%s: %d\n", __func__, ret);

	return ret;
}

static inline bool no_signal(struct v4l2_subdev *sd)
{
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);

	v4l2_dbg(1, debug, sd, "%s no signal:%d\n", __func__,
			lt7911uxc->nosignal);

	return lt7911uxc->nosignal;
}

static inline bool audio_present(struct v4l2_subdev *sd)
{
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);

	return lt7911uxc->is_audio_present;
}

static int get_audio_sampling_rate(struct v4l2_subdev *sd)
{
	static const int code_to_rate[] = {
		44100, 0, 48000, 32000, 22050, 384000, 24000, 352800,
		88200, 768000, 96000, 705600, 176400, 0, 192000, 0
	};

	if (no_signal(sd))
		return 0;

	return code_to_rate[2];
}

static inline unsigned int fps_calc(const struct v4l2_bt_timings *t)
{
	if (!V4L2_DV_BT_FRAME_HEIGHT(t) || !V4L2_DV_BT_FRAME_WIDTH(t))
		return 0;

	return DIV_ROUND_CLOSEST((unsigned int)t->pixelclock,
			V4L2_DV_BT_FRAME_HEIGHT(t) * V4L2_DV_BT_FRAME_WIDTH(t));
}

static bool lt7911uxc_rcv_supported_res(struct v4l2_subdev *sd, u32 width,
		u32 height)
{
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);
	u32 i;

	for (i = 0; i < lt7911uxc->cfg_num; i++) {
		if ((lt7911uxc->support_modes[i].width == width) &&
		    (lt7911uxc->support_modes[i].height == height)) {
			break;
		}
	}

	if (i == lt7911uxc->cfg_num) {
		v4l2_err(sd, "%s do not support res wxh: %dx%d\n", __func__,
				width, height);
		return false;
	} else {
		return true;
	}
}

static int lt7911uxc_get_detected_timings(struct v4l2_subdev *sd,
				     struct v4l2_dv_timings *timings)
{
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);
	struct v4l2_bt_timings *bt = &timings->bt;
	u32 hact, vact, htotal, vtotal;
	u32 pixel_clock, fps, halt_pix_clk;
	u8 clk_h, clk_m, clk_l;
	u8 val_h, val_l;
	u32 byte_clk, mipi_clk, mipi_data_rate;

	memset(timings, 0, sizeof(struct v4l2_dv_timings));

	clk_h = i2c_rd8(sd, PCLK_H);
	clk_m = i2c_rd8(sd, PCLK_M);
	clk_l = i2c_rd8(sd, PCLK_L);
	halt_pix_clk = ((clk_h << 16) | (clk_m << 8) | clk_l);
	pixel_clock = halt_pix_clk * 1000;

	clk_h = i2c_rd8(sd, BYTE_PCLK_H);
	clk_m = i2c_rd8(sd, BYTE_PCLK_M);
	clk_l = i2c_rd8(sd, BYTE_PCLK_L);
	byte_clk = ((clk_h << 16) | (clk_m << 8) | clk_l) * 1000;
	mipi_clk = byte_clk * 4;
	mipi_data_rate = byte_clk * 8;

	val_h = i2c_rd8(sd, HTOTAL_H);
	val_l = i2c_rd8(sd, HTOTAL_L);
	htotal = ((val_h << 8) | val_l);

	val_h = i2c_rd8(sd, VTOTAL_H);
	val_l = i2c_rd8(sd, VTOTAL_L);
	vtotal = (val_h << 8) | val_l;

	val_h = i2c_rd8(sd, HACT_H);
	val_l = i2c_rd8(sd, HACT_L);
	hact = ((val_h << 8) | val_l);

	val_h = i2c_rd8(sd, VACT_H);
	val_l = i2c_rd8(sd, VACT_L);
	vact = (val_h << 8) | val_l;

	if (!lt7911uxc_rcv_supported_res(sd, hact, vact)) {
		lt7911uxc->nosignal = true;
		v4l2_err(sd, "%s: rcv err res, return no signal!\n", __func__);
		return -EINVAL;
	}

	lt7911uxc->nosignal = false;
	lt7911uxc->is_audio_present = true;
	timings->type = V4L2_DV_BT_656_1120;
	bt->interlaced = V4L2_DV_PROGRESSIVE;
	bt->width = hact;
	bt->height = vact;
	bt->pixelclock = pixel_clock;
	fps = pixel_clock / (htotal * vtotal);

	v4l2_info(sd, "act:%dx%d, total:%dx%d, pixclk:%d, fps:%d\n",
			hact, vact, htotal, vtotal, pixel_clock, fps);
	v4l2_info(sd, "byte_clk:%d, mipi_clk:%d, mipi_data_rate:%d\n",
			byte_clk, mipi_clk, mipi_data_rate);
	v4l2_info(sd, "inerlaced:%d\n", bt->interlaced);

	return 0;
}

static void lt7911uxc_delayed_work_hotplug(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct lt7911uxc *lt7911uxc = container_of(dwork,
			struct lt7911uxc, delayed_work_hotplug);
	struct v4l2_subdev *sd = &lt7911uxc->sd;

	lt7911uxc_s_ctrl_detect_tx_5v(sd);
}

static void lt7911uxc_delayed_work_res_change(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct lt7911uxc *lt7911uxc = container_of(dwork,
			struct lt7911uxc, delayed_work_res_change);
	struct v4l2_subdev *sd = &lt7911uxc->sd;

	lt7911uxc_format_change(sd);
}

static int lt7911uxc_s_ctrl_detect_tx_5v(struct v4l2_subdev *sd)
{
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);

	return v4l2_ctrl_s_ctrl(lt7911uxc->detect_tx_5v_ctrl,
			tx_5v_power_present(sd));
}

static int lt7911uxc_s_ctrl_audio_sampling_rate(struct v4l2_subdev *sd)
{
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);

	return v4l2_ctrl_s_ctrl(lt7911uxc->audio_sampling_rate_ctrl,
			get_audio_sampling_rate(sd));
}

static int lt7911uxc_s_ctrl_audio_present(struct v4l2_subdev *sd)
{
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);

	return v4l2_ctrl_s_ctrl(lt7911uxc->audio_present_ctrl,
			audio_present(sd));
}

static int lt7911uxc_update_controls(struct v4l2_subdev *sd)
{
	int ret = 0;

	ret |= lt7911uxc_s_ctrl_detect_tx_5v(sd);
	ret |= lt7911uxc_s_ctrl_audio_sampling_rate(sd);
	ret |= lt7911uxc_s_ctrl_audio_present(sd);

	return ret;
}

static void lt7911uxc_cphy_timing_config(struct v4l2_subdev *sd)
{
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);

	if (lt7911uxc->bus_cfg.bus_type == V4L2_MBUS_CSI2_CPHY) {
		lt7911uxc_i2c_enable(sd);
		while (i2c_rd8(sd, HS_RQST_PRE_REG) != 0x3c) {
			i2c_wr8(sd, HS_RQST_PRE_REG, 0x3c);
			usleep_range(500, 600);
		}
		// i2c_wr8(sd, HS_TRAIL, 0x0b);
		lt7911uxc_i2c_disable(sd);
	}

	v4l2_dbg(1, debug, sd, "%s config timing succeed\n", __func__);
}

static bool lt7911uxc_match_timings(const struct v4l2_dv_timings *t1,
					const struct v4l2_dv_timings *t2)
{
	if (t1->type != t2->type || t1->type != V4L2_DV_BT_656_1120)
		return false;
	if (t1->bt.width == t2->bt.width &&
		t1->bt.height == t2->bt.height &&
		t1->bt.interlaced == t2->bt.interlaced)
		return true;

	return false;
}

static inline void enable_stream(struct v4l2_subdev *sd, bool enable)
{
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);

	lt7911uxc_cphy_timing_config(&lt7911uxc->sd);

	if (enable)
		i2c_wr8(&lt7911uxc->sd, STREAM_CTL, ENABLE_STREAM);
	else
		i2c_wr8(&lt7911uxc->sd, STREAM_CTL, DISABLE_STREAM);
	msleep(50);

	v4l2_dbg(2, debug, sd, "%s: %sable\n",
			__func__, enable ? "en" : "dis");
}

static void lt7911uxc_format_change(struct v4l2_subdev *sd)
{
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);
	struct v4l2_dv_timings timings;
	const struct v4l2_event lt7911uxc_ev_fmt = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
	};

	if (lt7911uxc_get_detected_timings(sd, &timings)) {
		enable_stream(sd, false);
		v4l2_dbg(1, debug, sd, "%s: No signal\n", __func__);
	}

	if (!lt7911uxc_match_timings(&lt7911uxc->timings, &timings)) {
		enable_stream(sd, false);
		/* automatically set timing rather than set by user */
		lt7911uxc_s_dv_timings(sd, &timings);
		v4l2_print_dv_timings(sd->name,
				"Format_change: New format: ",
				&timings, false);
		if (sd->devnode)
			v4l2_subdev_notify_event(sd, &lt7911uxc_ev_fmt);
	}
}

static int lt7911uxc_isr(struct v4l2_subdev *sd, u32 status, bool *handled)
{
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);

	schedule_delayed_work(&lt7911uxc->delayed_work_res_change, HZ / 20);
	*handled = true;

	return 0;
}

static irqreturn_t lt7911uxc_res_change_irq_handler(int irq, void *dev_id)
{
	struct lt7911uxc *lt7911uxc = dev_id;
	bool handled;

	lt7911uxc_isr(&lt7911uxc->sd, 0, &handled);

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

static irqreturn_t plugin_detect_irq_handler(int irq, void *dev_id)
{
	struct lt7911uxc *lt7911uxc = dev_id;

	/* control hpd output level after 25ms */
	schedule_delayed_work(&lt7911uxc->delayed_work_hotplug,
			HZ / 40);

	return IRQ_HANDLED;
}

static void lt7911uxc_irq_poll_timer(struct timer_list *t)
{
	struct lt7911uxc *lt7911uxc = from_timer(lt7911uxc, t, timer);

	schedule_work(&lt7911uxc->work_i2c_poll);
	mod_timer(&lt7911uxc->timer, jiffies + msecs_to_jiffies(POLL_INTERVAL_MS));
}

static void lt7911uxc_work_i2c_poll(struct work_struct *work)
{
	struct lt7911uxc *lt7911uxc = container_of(work,
			struct lt7911uxc, work_i2c_poll);
	struct v4l2_subdev *sd = &lt7911uxc->sd;

	lt7911uxc_format_change(sd);
}

static int lt7911uxc_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				    struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subdev_subscribe(sd, fh, sub);
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subdev_subscribe_event(sd, fh, sub);
	default:
		return -EINVAL;
	}
}

static int lt7911uxc_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	*status = 0;
	*status |= no_signal(sd) ? V4L2_IN_ST_NO_SIGNAL : 0;

	v4l2_dbg(1, debug, sd, "%s: status = 0x%x\n", __func__, *status);

	return 0;
}

static int lt7911uxc_s_dv_timings(struct v4l2_subdev *sd,
				 struct v4l2_dv_timings *timings)
{
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);

	if (!timings)
		return -EINVAL;

	if (debug)
		v4l2_print_dv_timings(sd->name, "s_dv_timings: ",
				timings, false);

	if (lt7911uxc_match_timings(&lt7911uxc->timings, timings)) {
		v4l2_dbg(1, debug, sd, "%s: no change\n", __func__);
		return 0;
	}

	if (!v4l2_valid_dv_timings(timings,
				&lt7911uxc_timings_cap, NULL, NULL)) {
		v4l2_dbg(1, debug, sd, "%s: timings out of range\n", __func__);
		return -ERANGE;
	}

	lt7911uxc->timings = *timings;

	enable_stream(sd, false);

	return 0;
}

static int lt7911uxc_g_dv_timings(struct v4l2_subdev *sd,
				struct v4l2_dv_timings *timings)
{
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);

	*timings = lt7911uxc->timings;

	return 0;
}

static int lt7911uxc_enum_dv_timings(struct v4l2_subdev *sd,
				struct v4l2_enum_dv_timings *timings)
{
	if (timings->pad != 0)
		return -EINVAL;

	return v4l2_enum_dv_timings_cap(timings,
			&lt7911uxc_timings_cap, NULL, NULL);
}

static int lt7911uxc_query_dv_timings(struct v4l2_subdev *sd,
				struct v4l2_dv_timings *timings)
{
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);

	*timings = lt7911uxc->timings;
	if (debug)
		v4l2_print_dv_timings(sd->name,
				"query_dv_timings: ", timings, false);

	if (!v4l2_valid_dv_timings(timings, &lt7911uxc_timings_cap, NULL,
				NULL)) {
		v4l2_dbg(1, debug, sd, "%s: timings out of range\n",
				__func__);

		return -ERANGE;
	}

	return 0;
}

static int lt7911uxc_dv_timings_cap(struct v4l2_subdev *sd,
				struct v4l2_dv_timings_cap *cap)
{
	if (cap->pad != 0)
		return -EINVAL;

	*cap = lt7911uxc_timings_cap;

	return 0;
}

static int lt7911uxc_g_mbus_config(struct v4l2_subdev *sd,
			unsigned int pad, struct v4l2_mbus_config *cfg)
{
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);
	u32 lane_num = lt7911uxc->bus_cfg.bus.mipi_csi2.num_data_lanes;
	u32 val = 0;

	val = 1 << (lane_num - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	cfg->type = lt7911uxc->bus_cfg.bus_type;
	cfg->flags = val;

	return 0;
}

static int lt7911uxc_s_stream(struct v4l2_subdev *sd, int on)
{
	enable_stream(sd, on);

	return 0;
}

static int lt7911uxc_enum_mbus_code(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_mbus_code_enum *code)
{
	switch (code->index) {
	case 0:
		code->code = MEDIA_BUS_FMT_UYVY8_2X8;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int lt7911uxc_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);

	if (fse->index >= lt7911uxc->cfg_num)
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_UYVY8_2X8)
		return -EINVAL;

	fse->min_width  = lt7911uxc->support_modes[fse->index].width;
	fse->max_width  = lt7911uxc->support_modes[fse->index].width;
	fse->max_height = lt7911uxc->support_modes[fse->index].height;
	fse->min_height = lt7911uxc->support_modes[fse->index].height;

	return 0;
}

static int lt7911uxc_get_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_format *format)
{
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);

	mutex_lock(&lt7911uxc->confctl_mutex);
	format->format.code = lt7911uxc->mbus_fmt_code;
	format->format.width = lt7911uxc->timings.bt.width;
	format->format.height = lt7911uxc->timings.bt.height;
	format->format.field =
		lt7911uxc->timings.bt.interlaced ?
		V4L2_FIELD_INTERLACED : V4L2_FIELD_NONE;
	format->format.colorspace = V4L2_COLORSPACE_SRGB;
	mutex_unlock(&lt7911uxc->confctl_mutex);

	v4l2_dbg(1, debug, sd, "%s: fmt code:%d, w:%d, h:%d, field code:%d\n",
			__func__, format->format.code, format->format.width,
			format->format.height, format->format.field);

	return 0;
}

static int lt7911uxc_enum_frame_interval(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_frame_interval_enum *fie)
{
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);

	if (fie->index >= lt7911uxc->cfg_num)
		return -EINVAL;

	if (fie->code != MEDIA_BUS_FMT_UYVY8_2X8)
		return -EINVAL;

	fie->width = lt7911uxc->support_modes[fie->index].width;
	fie->height = lt7911uxc->support_modes[fie->index].height;
	fie->interval = lt7911uxc->support_modes[fie->index].max_fps;

	return 0;
}

static int lt7911uxc_get_reso_dist(const struct lt7911uxc_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
		abs(mode->height - framefmt->height);
}

static const struct lt7911uxc_mode *
lt7911uxc_find_best_fit(struct lt7911uxc *lt7911uxc, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < lt7911uxc->cfg_num; i++) {
		dist = lt7911uxc_get_reso_dist(&lt7911uxc->support_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &lt7911uxc->support_modes[cur_best_fit];
}

static int lt7911uxc_set_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_format *format)
{
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);
	const struct lt7911uxc_mode *mode;

	/* is overwritten by get_fmt */
	u32 code = format->format.code;
	int ret = lt7911uxc_get_fmt(sd, cfg, format);

	format->format.code = code;

	if (ret)
		return ret;

	switch (code) {
	case MEDIA_BUS_FMT_UYVY8_2X8:
		break;

	default:
		return -EINVAL;
	}

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	lt7911uxc->mbus_fmt_code = format->format.code;
	mode = lt7911uxc_find_best_fit(lt7911uxc, format);
	lt7911uxc->cur_mode = mode;

	__v4l2_ctrl_s_ctrl_int64(lt7911uxc->pixel_rate,
				LT7911UXC_PIXEL_RATE);
	__v4l2_ctrl_s_ctrl(lt7911uxc->link_freq,
				mode->mipi_freq_idx);

	enable_stream(sd, false);

	dev_info(&lt7911uxc->i2c_client->dev, "%s: mode->mipi_freq_idx(%d)",
		 __func__, mode->mipi_freq_idx);

	return 0;
}

static int lt7911uxc_g_frame_interval(struct v4l2_subdev *sd,
			struct v4l2_subdev_frame_interval *fi)
{
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);
	const struct lt7911uxc_mode *mode = lt7911uxc->cur_mode;

	mutex_lock(&lt7911uxc->confctl_mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&lt7911uxc->confctl_mutex);

	return 0;
}

static void lt7911uxc_get_module_inf(struct lt7911uxc *lt7911uxc,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, LT7911UXC_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, lt7911uxc->module_name, sizeof(inf->base.module));
	strscpy(inf->base.lens, lt7911uxc->len_name, sizeof(inf->base.lens));
}

static long lt7911uxc_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);
	long ret = 0;
	struct rkmodule_csi_dphy_param *dphy_param;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		lt7911uxc_get_module_inf(lt7911uxc, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDMI_MODE:
		*(int *)arg = RKMODULE_HDMIIN_MODE;
		break;
	case RKMODULE_SET_CSI_DPHY_PARAM:
		dphy_param = (struct rkmodule_csi_dphy_param *)arg;
		if (dphy_param->vendor == rk3588_dcphy_param.vendor)
			rk3588_dcphy_param = *dphy_param;
		dev_dbg(&lt7911uxc->i2c_client->dev,
			"sensor set dphy param\n");
		break;
	case RKMODULE_GET_CSI_DPHY_PARAM:
		dphy_param = (struct rkmodule_csi_dphy_param *)arg;
		if (dphy_param->vendor == rk3588_dcphy_param.vendor)
			*dphy_param = rk3588_dcphy_param;
		dev_dbg(&lt7911uxc->i2c_client->dev,
			"sensor get dphy param\n");
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

static int lt7911uxc_s_power(struct v4l2_subdev *sd, int on)
{
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);
	int ret = 0;

	mutex_lock(&lt7911uxc->confctl_mutex);

	if (lt7911uxc->power_on == !!on)
		goto unlock_and_return;

	if (on)
		lt7911uxc->power_on = true;
	else
		lt7911uxc->power_on = false;

unlock_and_return:
	mutex_unlock(&lt7911uxc->confctl_mutex);

	return ret;
}

#ifdef CONFIG_COMPAT
static long lt7911uxc_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	long ret;
	int *seq;
	struct rkmodule_csi_dphy_param *dphy_param;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = lt7911uxc_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(inf);
		break;
	case RKMODULE_GET_HDMI_MODE:
		seq = kzalloc(sizeof(*seq), GFP_KERNEL);
		if (!seq) {
			ret = -ENOMEM;
			return ret;
		}

		ret = lt7911uxc_ioctl(sd, cmd, seq);
		if (!ret) {
			ret = copy_to_user(up, seq, sizeof(*seq));
			if (ret)
				ret = -EFAULT;
		}
		kfree(seq);
		break;
	case RKMODULE_SET_CSI_DPHY_PARAM:
		dphy_param = kzalloc(sizeof(*dphy_param), GFP_KERNEL);
		if (!dphy_param) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(dphy_param, up, sizeof(*dphy_param));
		if (!ret)
			ret = lt7911uxc_ioctl(sd, cmd, dphy_param);
		else
			ret = -EFAULT;
		kfree(dphy_param);
		break;
	case RKMODULE_GET_CSI_DPHY_PARAM:
		dphy_param = kzalloc(sizeof(*dphy_param), GFP_KERNEL);
		if (!dphy_param) {
			ret = -ENOMEM;
			return ret;
		}

		ret = lt7911uxc_ioctl(sd, cmd, dphy_param);
		if (!ret) {
			ret = copy_to_user(up, dphy_param, sizeof(*dphy_param));
			if (ret)
				ret = -EFAULT;
		}
		kfree(dphy_param);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int lt7911uxc_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct lt7911uxc_mode *def_mode = &lt7911uxc->support_modes[0];

	mutex_lock(&lt7911uxc->confctl_mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_UYVY8_2X8;
	try_fmt->field = V4L2_FIELD_NONE;
	mutex_unlock(&lt7911uxc->confctl_mutex);

	return 0;
}
#endif

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops lt7911uxc_internal_ops = {
	.open = lt7911uxc_open,
};
#endif

static const struct v4l2_subdev_core_ops lt7911uxc_core_ops = {
	.s_power = lt7911uxc_s_power,
	.interrupt_service_routine = lt7911uxc_isr,
	.subscribe_event = lt7911uxc_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
	.ioctl = lt7911uxc_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = lt7911uxc_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops lt7911uxc_video_ops = {
	.g_input_status = lt7911uxc_g_input_status,
	.s_dv_timings = lt7911uxc_s_dv_timings,
	.g_dv_timings = lt7911uxc_g_dv_timings,
	.query_dv_timings = lt7911uxc_query_dv_timings,
	.s_stream = lt7911uxc_s_stream,
	.g_frame_interval = lt7911uxc_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops lt7911uxc_pad_ops = {
	.enum_mbus_code = lt7911uxc_enum_mbus_code,
	.enum_frame_size = lt7911uxc_enum_frame_sizes,
	.enum_frame_interval = lt7911uxc_enum_frame_interval,
	.set_fmt = lt7911uxc_set_fmt,
	.get_fmt = lt7911uxc_get_fmt,
	.enum_dv_timings = lt7911uxc_enum_dv_timings,
	.dv_timings_cap = lt7911uxc_dv_timings_cap,
	.get_mbus_config = lt7911uxc_g_mbus_config,
};

static const struct v4l2_subdev_ops lt7911uxc_ops = {
	.core = &lt7911uxc_core_ops,
	.video = &lt7911uxc_video_ops,
	.pad = &lt7911uxc_pad_ops,
};

static const struct v4l2_ctrl_config lt7911uxc_ctrl_audio_sampling_rate = {
	.id = RK_V4L2_CID_AUDIO_SAMPLING_RATE,
	.name = "Audio sampling rate",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 768000,
	.step = 1,
	.def = 0,
	.flags = V4L2_CTRL_FLAG_READ_ONLY,
};

static const struct v4l2_ctrl_config lt7911uxc_ctrl_audio_present = {
	.id = RK_V4L2_CID_AUDIO_PRESENT,
	.name = "Audio present",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 0,
	.flags = V4L2_CTRL_FLAG_READ_ONLY,
};

static void lt7911uxc_reset(struct lt7911uxc *lt7911uxc)
{
	gpiod_set_value(lt7911uxc->reset_gpio, 0);
	usleep_range(2000, 2100);
	gpiod_set_value(lt7911uxc->reset_gpio, 1);
	usleep_range(120*1000, 121*1000);
	gpiod_set_value(lt7911uxc->reset_gpio, 0);
	usleep_range(300*1000, 310*1000);
}

static int lt7911uxc_init_v4l2_ctrls(struct lt7911uxc *lt7911uxc)
{
	const struct lt7911uxc_mode *mode;
	struct v4l2_subdev *sd;
	int ret;

	mode = lt7911uxc->cur_mode;
	sd = &lt7911uxc->sd;
	ret = v4l2_ctrl_handler_init(&lt7911uxc->hdl, 5);
	if (ret)
		return ret;

	lt7911uxc->link_freq = v4l2_ctrl_new_int_menu(&lt7911uxc->hdl, NULL,
			V4L2_CID_LINK_FREQ,
			ARRAY_SIZE(link_freq_menu_items) - 1, 0,
			link_freq_menu_items);
	lt7911uxc->pixel_rate = v4l2_ctrl_new_std(&lt7911uxc->hdl, NULL,
			V4L2_CID_PIXEL_RATE,
			0, LT7911UXC_PIXEL_RATE, 1, LT7911UXC_PIXEL_RATE);

	lt7911uxc->detect_tx_5v_ctrl = v4l2_ctrl_new_std(&lt7911uxc->hdl,
			NULL, V4L2_CID_DV_RX_POWER_PRESENT,
			0, 1, 0, 0);

	lt7911uxc->audio_sampling_rate_ctrl =
		v4l2_ctrl_new_custom(&lt7911uxc->hdl,
				&lt7911uxc_ctrl_audio_sampling_rate, NULL);
	lt7911uxc->audio_present_ctrl = v4l2_ctrl_new_custom(&lt7911uxc->hdl,
			&lt7911uxc_ctrl_audio_present, NULL);

	sd->ctrl_handler = &lt7911uxc->hdl;
	if (lt7911uxc->hdl.error) {
		ret = lt7911uxc->hdl.error;
		v4l2_err(sd, "cfg v4l2 ctrls failed! ret:%d\n", ret);
		return ret;
	}

	__v4l2_ctrl_s_ctrl(lt7911uxc->link_freq, mode->mipi_freq_idx);
	__v4l2_ctrl_s_ctrl_int64(lt7911uxc->pixel_rate, LT7911UXC_PIXEL_RATE);

	if (lt7911uxc_update_controls(sd)) {
		ret = -ENODEV;
		v4l2_err(sd, "update v4l2 ctrls failed! ret:%d\n", ret);
		return ret;
	}

	return 0;
}

#ifdef CONFIG_OF
static int lt7911uxc_probe_of(struct lt7911uxc *lt7911uxc)
{
	struct device *dev = &lt7911uxc->i2c_client->dev;
	struct device_node *node = dev->of_node;
	struct device_node *ep;
	int ret;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
			&lt7911uxc->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
			&lt7911uxc->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
			&lt7911uxc->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
			&lt7911uxc->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	lt7911uxc->power_gpio = devm_gpiod_get_optional(dev, "power",
			GPIOD_OUT_LOW);
	if (IS_ERR(lt7911uxc->power_gpio)) {
		dev_err(dev, "failed to get power gpio\n");
		ret = PTR_ERR(lt7911uxc->power_gpio);
		return ret;
	}

	lt7911uxc->reset_gpio = devm_gpiod_get_optional(dev, "reset",
			GPIOD_OUT_HIGH);
	if (IS_ERR(lt7911uxc->reset_gpio)) {
		dev_err(dev, "failed to get reset gpio\n");
		ret = PTR_ERR(lt7911uxc->reset_gpio);
		return ret;
	}

	lt7911uxc->plugin_det_gpio = devm_gpiod_get_optional(dev, "plugin-det",
			GPIOD_IN);
	if (IS_ERR(lt7911uxc->plugin_det_gpio)) {
		dev_err(dev, "failed to get plugin det gpio\n");
		ret = PTR_ERR(lt7911uxc->plugin_det_gpio);
		return ret;
	}

	ep = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!ep) {
		dev_err(dev, "missing endpoint node\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(ep),
					&lt7911uxc->bus_cfg);
	if (ret) {
		dev_err(dev, "failed to parse endpoint\n");
		goto put_node;
	}

	if (lt7911uxc->bus_cfg.bus_type == V4L2_MBUS_CSI2_DPHY) {
		lt7911uxc->support_modes = supported_modes_dphy;
		lt7911uxc->cfg_num = ARRAY_SIZE(supported_modes_dphy);
	} else {
		lt7911uxc->support_modes = supported_modes_cphy;
		lt7911uxc->cfg_num = ARRAY_SIZE(supported_modes_cphy);
	}

	lt7911uxc->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(lt7911uxc->xvclk)) {
		dev_err(dev, "failed to get xvclk\n");
		ret = -EINVAL;
		goto put_node;
	}

	ret = clk_prepare_enable(lt7911uxc->xvclk);
	if (ret) {
		dev_err(dev, "Failed! to enable xvclk\n");
		goto put_node;
	}

	lt7911uxc->enable_hdcp = false;

	gpiod_set_value(lt7911uxc->power_gpio, 1);
	lt7911uxc_reset(lt7911uxc);

	ret = 0;

put_node:
	of_node_put(ep);
	return ret;
}
#else
static inline int lt7911uxc_probe_of(struct lt7911uxc *state)
{
	return -ENODEV;
}
#endif
static int lt7911uxc_check_chip_id(struct lt7911uxc *lt7911uxc)
{
	struct device *dev = &lt7911uxc->i2c_client->dev;
	struct v4l2_subdev *sd = &lt7911uxc->sd;
	u8 id_h, id_l;
	u32 chipid;
	int ret = 0;

	lt7911uxc_i2c_enable(sd);
	id_l  = i2c_rd8(sd, CHIPID_REGL);
	id_h  = i2c_rd8(sd, CHIPID_REGH);
	lt7911uxc_i2c_disable(sd);

	chipid = (id_h << 8) | id_l;
	if (chipid != LT7911UXC_CHIPID) {
		dev_err(dev, "chipid err, read:%#x, expect:%#x\n",
				chipid, LT7911UXC_CHIPID);
		return -EINVAL;
	}
	dev_info(dev, "check chipid ok, id:%#x", chipid);

	return ret;
}

static int lt7911uxc_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct lt7911uxc *lt7911uxc;
	struct v4l2_subdev *sd;
	struct device *dev = &client->dev;
	char facing[2];
	int err;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	lt7911uxc = devm_kzalloc(dev, sizeof(struct lt7911uxc), GFP_KERNEL);
	if (!lt7911uxc)
		return -ENOMEM;

	sd = &lt7911uxc->sd;
	lt7911uxc->i2c_client = client;
	lt7911uxc->mbus_fmt_code = MEDIA_BUS_FMT_UYVY8_2X8;

	err = lt7911uxc_probe_of(lt7911uxc);
	if (err) {
		v4l2_err(sd, "lt7911uxc_parse_of failed! err:%d\n", err);
		return err;
	}

	lt7911uxc->cur_mode = &lt7911uxc->support_modes[0];
	err = lt7911uxc_check_chip_id(lt7911uxc);
	if (err < 0)
		return err;

	mutex_init(&lt7911uxc->confctl_mutex);
	err = lt7911uxc_init_v4l2_ctrls(lt7911uxc);
	if (err)
		goto err_free_hdl;

	client->flags |= I2C_CLIENT_SCCB;
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	v4l2_i2c_subdev_init(sd, client, &lt7911uxc_ops);
	sd->internal_ops = &lt7911uxc_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
#endif

#if defined(CONFIG_MEDIA_CONTROLLER)
	lt7911uxc->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	err = media_entity_pads_init(&sd->entity, 1, &lt7911uxc->pad);
	if (err < 0) {
		v4l2_err(sd, "media entity init failed! err:%d\n", err);
		goto err_free_hdl;
	}
#endif
	memset(facing, 0, sizeof(facing));
	if (strcmp(lt7911uxc->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 lt7911uxc->module_index, facing,
		 LT7911UXC_NAME, dev_name(sd->dev));
	err = v4l2_async_register_subdev_sensor_common(sd);
	if (err < 0) {
		v4l2_err(sd, "v4l2 register subdev failed! err:%d\n", err);
		goto err_clean_entity;
	}

	INIT_DELAYED_WORK(&lt7911uxc->delayed_work_hotplug,
			lt7911uxc_delayed_work_hotplug);
	INIT_DELAYED_WORK(&lt7911uxc->delayed_work_res_change,
			lt7911uxc_delayed_work_res_change);

	if (lt7911uxc->i2c_client->irq) {
		v4l2_dbg(1, debug, sd, "cfg lt7911uxc irq!\n");
		err = devm_request_threaded_irq(dev,
				lt7911uxc->i2c_client->irq,
				NULL, lt7911uxc_res_change_irq_handler,
				IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				"lt7911uxc", lt7911uxc);
		if (err) {
			v4l2_err(sd, "request irq failed! err:%d\n", err);
			goto err_work_queues;
		}
	} else {
		v4l2_dbg(1, debug, sd, "no irq, cfg poll!\n");
		INIT_WORK(&lt7911uxc->work_i2c_poll, lt7911uxc_work_i2c_poll);
		timer_setup(&lt7911uxc->timer, lt7911uxc_irq_poll_timer, 0);
		lt7911uxc->timer.expires = jiffies +
				       msecs_to_jiffies(POLL_INTERVAL_MS);
		add_timer(&lt7911uxc->timer);
	}

	lt7911uxc->plugin_irq = gpiod_to_irq(lt7911uxc->plugin_det_gpio);
	if (lt7911uxc->plugin_irq < 0)
		dev_err(dev, "failed to get plugin det irq, maybe no use\n");

	err = devm_request_threaded_irq(dev, lt7911uxc->plugin_irq, NULL,
			plugin_detect_irq_handler, IRQF_TRIGGER_FALLING |
			IRQF_TRIGGER_RISING | IRQF_ONESHOT, "lt7911uxc",
			lt7911uxc);
	if (err)
		dev_err(dev, "failed to register plugin det irq (%d), maybe no use\n", err);

	err = v4l2_ctrl_handler_setup(sd->ctrl_handler);
	if (err) {
		v4l2_err(sd, "v4l2 ctrl handler setup failed! err:%d\n", err);
		goto err_work_queues;
	}

	v4l2_info(sd, "%s found @ 0x%x (%s)\n", client->name,
			client->addr << 1, client->adapter->name);

	return 0;

err_work_queues:
	if (!lt7911uxc->i2c_client->irq)
		flush_work(&lt7911uxc->work_i2c_poll);
	cancel_delayed_work(&lt7911uxc->delayed_work_hotplug);
	cancel_delayed_work(&lt7911uxc->delayed_work_res_change);
err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_free_hdl:
	v4l2_ctrl_handler_free(&lt7911uxc->hdl);
	mutex_destroy(&lt7911uxc->confctl_mutex);
	return err;
}

static int lt7911uxc_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct lt7911uxc *lt7911uxc = to_lt7911uxc(sd);

	if (!lt7911uxc->i2c_client->irq) {
		del_timer_sync(&lt7911uxc->timer);
		flush_work(&lt7911uxc->work_i2c_poll);
	}
	cancel_delayed_work_sync(&lt7911uxc->delayed_work_hotplug);
	cancel_delayed_work_sync(&lt7911uxc->delayed_work_res_change);
	v4l2_async_unregister_subdev(sd);
	v4l2_device_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&lt7911uxc->hdl);
	mutex_destroy(&lt7911uxc->confctl_mutex);
	clk_disable_unprepare(lt7911uxc->xvclk);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id lt7911uxc_of_match[] = {
	{ .compatible = "lontium,lt7911uxc" },
	{},
};
MODULE_DEVICE_TABLE(of, lt7911uxc_of_match);
#endif

static struct i2c_driver lt7911uxc_driver = {
	.driver = {
		.name = LT7911UXC_NAME,
		.of_match_table = of_match_ptr(lt7911uxc_of_match),
	},
	.probe = lt7911uxc_probe,
	.remove = lt7911uxc_remove,
};

static int __init lt7911uxc_driver_init(void)
{
	return i2c_add_driver(&lt7911uxc_driver);
}

static void __exit lt7911uxc_driver_exit(void)
{
	i2c_del_driver(&lt7911uxc_driver);
}

device_initcall_sync(lt7911uxc_driver_init);
module_exit(lt7911uxc_driver_exit);

MODULE_DESCRIPTION("Lontium lt7911uxc DP/type-c to CSI-2 bridge driver");
MODULE_AUTHOR("Jianwei Fan <jianwei.fan@rock-chips.com>");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Dingxian Wen <shawn.wen@rock-chips.com>
 *
 * V0.0X01.0X00 first version.
 * V0.0X01.0X01
 * 1. add BT656 mode support.
 * 2. add ddr mode support.
 * 3. fix 576i and 480i support mode.
 * V0.0X01.0X02 add 4K30 mode.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/of_graph.h>
#include <linux/videodev2.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/v4l2-dv-timings.h>
#include <linux/hdmi.h>
#include <linux/version.h>
#include <linux/compat.h>
#include <linux/rk-camera-module.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <linux/regmap.h>
#include "lt8619c.h"

#define DRIVER_VERSION		KERNEL_VERSION(0, 0x01, 0x02)
#define LT8619C_NAME		"LT8619C"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-2)");

#define RK_CAMERA_MODULE_DUAL_EDGE		"rockchip,dual-edge"
#define LT8619C_DEFAULT_DUAL_EDGE		1U
#define RK_CAMERA_MODULE_DVP_MODE		"rockchip,dvp-mode"
#define LT8619C_DEFAULT_DVP_MODE		BT1120_OUTPUT

struct lt8619c_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
};

struct lt8619c {
	struct device *dev;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler hdl;
	struct i2c_client *i2c_client;
	struct mutex confctl_mutex;
	struct v4l2_ctrl *detect_tx_5v_ctrl;
	struct delayed_work delayed_work_enable_hotplug;
	struct delayed_work delayed_work_monitor_resolution;
	struct v4l2_dv_timings timings;
	struct regmap *reg_map;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *power_gpio;
	struct gpio_desc *plugin_det_gpio;
	struct clk *xvclk;
	const struct lt8619c_mode *cur_mode;
	const char *module_facing;
	const char *module_name;
	const char *len_name;
	bool nosignal;
	bool enable_hdcp;
	u32 clk_ddrmode_en;
	bool BT656_double_clk_en;
	bool hpd_output_inverted;
	int plugin_irq;
	u32 edid_blocks_written;
	u32 mbus_fmt_code;
	u32 module_index;
	u32 yuv_output_mode;
	u32 cp_convert_mode;
	u32 yc_swap;
	u32 yuv_colordepth;
	u32 bt_tx_sync_pol;
};

static const struct v4l2_dv_timings_cap lt8619c_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	/* keep this initialization for compatibility with GCC < 4.4.6 */
	.reserved = { 0 },
	V4L2_INIT_BT_TIMINGS(1, 10000, 1, 10000, 0, 410000000,
			V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
			V4L2_DV_BT_STD_GTF | V4L2_DV_BT_STD_CVT,
			V4L2_DV_BT_CAP_PROGRESSIVE | V4L2_DV_BT_CAP_INTERLACED |
			V4L2_DV_BT_CAP_REDUCED_BLANKING |
			V4L2_DV_BT_CAP_CUSTOM)
};

static u8 edid_init_data[] = {
	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
	0x49, 0x78, 0x01, 0x88, 0x00, 0x88, 0x88, 0x88,
	0x1C, 0x1F, 0x01, 0x03, 0x80, 0x00, 0x00, 0x78,
	0x0A, 0x0D, 0xC9, 0xA0, 0x57, 0x47, 0x98, 0x27,
	0x12, 0x48, 0x4C, 0x00, 0x00, 0x00, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x3A,
	0x80, 0x18, 0x71, 0x38, 0x2D, 0x40, 0x58, 0x2C,
	0x45, 0x00, 0xC4, 0x8E, 0x21, 0x00, 0x00, 0x1E,
	0x01, 0x1D, 0x00, 0x72, 0x51, 0xD0, 0x1E, 0x20,
	0x6E, 0x28, 0x55, 0x00, 0xC4, 0x8E, 0x21, 0x00,
	0x00, 0x1E, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x54,
	0x37, 0x34, 0x39, 0x2D, 0x66, 0x48, 0x44, 0x37,
	0x32, 0x30, 0x0A, 0x20, 0x00, 0x00, 0x00, 0xFD,
	0x00, 0x14, 0x78, 0x01, 0xFF, 0x1D, 0x00, 0x0A,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01, 0x64,

	0x02, 0x03, 0x1C, 0x71, 0x49, 0x90, 0x04, 0x02,
	0x5F, 0x11, 0x07, 0x05, 0x16, 0x22, 0x23, 0x09,
	0x07, 0x01, 0x83, 0x01, 0x00, 0x00, 0x65, 0x03,
	0x0C, 0x00, 0x10, 0x00, 0x8C, 0x0A, 0xD0, 0x8A,
	0x20, 0xE0, 0x2D, 0x10, 0x10, 0x3E, 0x96, 0x00,
	0x13, 0x8E, 0x21, 0x00, 0x00, 0x1E, 0xD8, 0x09,
	0x80, 0xA0, 0x20, 0xE0, 0x2D, 0x10, 0x10, 0x60,
	0xA2, 0x00, 0xC4, 0x8E, 0x21, 0x00, 0x00, 0x18,
	0x8C, 0x0A, 0xD0, 0x90, 0x20, 0x40, 0x31, 0x20,
	0x0C, 0x40, 0x55, 0x00, 0x48, 0x39, 0x00, 0x00,
	0x00, 0x18, 0x01, 0x1D, 0x80, 0x18, 0x71, 0x38,
	0x2D, 0x40, 0x58, 0x2C, 0x45, 0x00, 0xC0, 0x6C,
	0x00, 0x00, 0x00, 0x18, 0x01, 0x1D, 0x80, 0x18,
	0x71, 0x1C, 0x16, 0x20, 0x58, 0x2C, 0x25, 0x00,
	0xC0, 0x6C, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB3,
};

static u8 phase_num[10] = {
	0x20, 0x28, 0x21, 0x29, 0x22,
	0x2a, 0x23, 0x2b, 0x24, 0x2c,
};

static const struct lt8619c_mode supported_modes[] = {
	{
		.width = 3840,
		.height = 2160,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.hts_def = 4400,
		.vts_def = 2250,
	}, {
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 2200,
		.vts_def = 1125,
	}, {
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.hts_def = 2200,
		.vts_def = 1125,
	}, {
		.width = 1920,
		.height = 540,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 2200,
		.vts_def = 562,
	}, {
		.width = 1280,
		.height = 720,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 1650,
		.vts_def = 750,
	}, {
		.width = 720,
		.height = 576,
		.max_fps = {
			.numerator = 10000,
			.denominator = 500000,
		},
		.hts_def = 864,
		.vts_def = 625,
	}, {
		.width = 720,
		.height = 480,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 858,
		.vts_def = 525,
	}, {
		.width = 1440,
		.height = 480,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 1716,
		.vts_def = 525,
	}, {
		.width = 1440,
		.height = 576,
		.max_fps = {
			.numerator = 10000,
			.denominator = 500000,
		},
		.hts_def = 1728,
		.vts_def = 625,
	},
};

static void lt8619c_set_hpd(struct v4l2_subdev *sd, int en);
static void lt8619c_wait_for_signal_stable(struct v4l2_subdev *sd);
static void lt8619c_yuv_config(struct v4l2_subdev *sd);
static void lt8619c_format_change(struct v4l2_subdev *sd);
static void enable_stream(struct v4l2_subdev *sd, bool enable);
static int lt8619c_s_dv_timings(struct v4l2_subdev *sd,
			struct v4l2_dv_timings *timings);
static void LVDSPLL_Lock_Det(struct v4l2_subdev *sd);
static void LT8619C_phase_config(struct v4l2_subdev *sd);
static bool lt8619c_rcv_supported_res(struct v4l2_subdev *sd,
		struct v4l2_dv_timings *timings);
static bool lt8619c_timing_changed(struct v4l2_subdev *sd,
		struct v4l2_dv_timings *timings);

static inline struct lt8619c *to_lt8619c(struct v4l2_subdev *sd)
{
	return container_of(sd, struct lt8619c, sd);
}

static inline bool tx_5v_power_present(struct v4l2_subdev *sd)
{
	int val;
	struct lt8619c *lt8619c = to_lt8619c(sd);

	val = gpiod_get_value(lt8619c->plugin_det_gpio);
	v4l2_dbg(1, debug, sd, "%s 5v_present: %d!\n", __func__, val);
	return  (val > 0);
}

static inline bool no_signal(struct v4l2_subdev *sd)
{
	struct lt8619c *lt8619c = to_lt8619c(sd);

	v4l2_dbg(1, debug, sd, "no signal:%d\n", lt8619c->nosignal);
	return lt8619c->nosignal;
}

static bool lt8619c_is_supported_interlaced_res(struct v4l2_subdev *sd,
		u32 hact, u32 vact)
{
	if ((hact == 1920 && vact == 540) ||
	    (hact == 1440 && vact == 288) ||
	    (hact == 1440 && vact == 240))
		return true;

	return false;
}

static int lt8619c_get_detected_timings(struct v4l2_subdev *sd,
				     struct v4l2_dv_timings *timings)
{
	struct lt8619c *lt8619c = to_lt8619c(sd);
	struct v4l2_bt_timings *bt = &timings->bt;
	u32 hact, vact, htotal, vtotal, hbp, hfp, hs;
	u32 fps, hdmi_clk_cnt;
	u32 val, vbp, vfp, vs;
	u32 pix_clk;

	memset(timings, 0, sizeof(struct v4l2_dv_timings));
	timings->type = V4L2_DV_BT_656_1120;

	regmap_write(lt8619c->reg_map, BANK_REG, BANK_60);
	regmap_read(lt8619c->reg_map, 0x22, &val);
	hact = val << 8;
	regmap_read(lt8619c->reg_map, 0x23, &val);
	hact |= val;

	regmap_read(lt8619c->reg_map, 0x20, &val);
	vact = (val & 0xf) << 8;
	regmap_read(lt8619c->reg_map, 0x21, &val);
	vact |= val;

	regmap_read(lt8619c->reg_map, 0x1e, &val);
	htotal = val << 8;
	regmap_read(lt8619c->reg_map, 0x1f, &val);
	htotal |= val;

	regmap_read(lt8619c->reg_map, 0x1c, &val);
	vtotal = (val & 0xf) << 8;
	regmap_read(lt8619c->reg_map, 0x1d, &val);
	vtotal |= val;

	regmap_read(lt8619c->reg_map, 0x1a, &val);
	hfp = val << 8;
	regmap_read(lt8619c->reg_map, 0x1b, &val);
	hfp |= val;

	regmap_read(lt8619c->reg_map, 0x18, &val);
	hbp = val << 8;
	regmap_read(lt8619c->reg_map, 0x19, &val);
	hbp |= val;

	regmap_read(lt8619c->reg_map, 0x14, &val);
	hs = val << 8;
	regmap_read(lt8619c->reg_map, 0x15, &val);
	hs |= val;

	regmap_read(lt8619c->reg_map, 0x17, &vfp);
	regmap_read(lt8619c->reg_map, 0x16, &vbp);
	regmap_read(lt8619c->reg_map, 0x13, &vs);

	regmap_write(lt8619c->reg_map, BANK_REG, BANK_80);
	regmap_read(lt8619c->reg_map, 0x44, &val);
	hdmi_clk_cnt = (val & 0x3) << 16;
	regmap_read(lt8619c->reg_map, 0x45, &val);
	hdmi_clk_cnt |= val << 8;
	regmap_read(lt8619c->reg_map, 0x46, &val);
	hdmi_clk_cnt |= val;

	bt->width = hact;
	bt->height = vact;
	bt->hfrontporch = hfp;
	bt->hsync = hs;
	bt->hbackporch = hbp;
	bt->vfrontporch = vfp;
	bt->vsync = vs;
	bt->vbackporch = vbp;
	pix_clk = hdmi_clk_cnt * 1000;
	bt->pixelclock = pix_clk;

	fps = 0;
	if (htotal * vtotal)
		fps = (pix_clk + (htotal * vtotal) / 2) / (htotal * vtotal);

	/* for interlaced res 1080i 576i 480i */
	if (lt8619c_is_supported_interlaced_res(sd, hact, vact)) {
		bt->interlaced = V4L2_DV_INTERLACED;
		bt->height *= 2;
		bt->il_vsync = bt->vsync + 1;
	} else {
		bt->interlaced = V4L2_DV_PROGRESSIVE;
	}

	v4l2_dbg(1, debug, sd,
		"%s: act:%dx%d, total:%dx%d, fps:%d, pixclk:%llu, frame mode:%s\n",
		__func__, hact, vact, htotal, vtotal, fps, bt->pixelclock,
		(bt->interlaced == V4L2_DV_INTERLACED) ? "I" : "P");
	v4l2_dbg(1, debug, sd,
		"%s: hfp:%d, hs:%d, hbp:%d, vfp:%d, vs:%d, vbp:%d\n",
		__func__, bt->hfrontporch, bt->hsync, bt->hbackporch,
		bt->vfrontporch, bt->vsync, bt->vbackporch);

	return 0;
}

static void lt8619c_config_all(struct v4l2_subdev *sd)
{
	lt8619c_wait_for_signal_stable(sd);
	LVDSPLL_Lock_Det(sd);
	lt8619c_yuv_config(sd);
	LT8619C_phase_config(sd);
	lt8619c_format_change(sd);
}

static void lt8619c_delayed_work_enable_hotplug(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct lt8619c *lt8619c = container_of(dwork, struct lt8619c,
			delayed_work_enable_hotplug);
	struct v4l2_subdev *sd = &lt8619c->sd;

	v4l2_dbg(2, debug, sd, "%s: in\n", __func__);
	mutex_lock(&lt8619c->confctl_mutex);
	if (tx_5v_power_present(sd)) {
		lt8619c_set_hpd(sd, 1);
		lt8619c_config_all(sd);
		lt8619c->nosignal = false;
		/* monitor resolution after 100ms */
		schedule_delayed_work(&lt8619c->delayed_work_monitor_resolution,
				HZ / 10);
	} else {
		cancel_delayed_work(&lt8619c->delayed_work_monitor_resolution);
		enable_stream(sd, false);
		lt8619c_set_hpd(sd, 0);
		lt8619c->nosignal = true;
	}
	mutex_unlock(&lt8619c->confctl_mutex);
}

static void lt8619c_delayed_work_monitor_resolution(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct lt8619c *lt8619c = container_of(dwork, struct lt8619c,
			delayed_work_monitor_resolution);
	struct v4l2_subdev *sd = &lt8619c->sd;
	struct v4l2_dv_timings timings;
	bool is_supported_res, is_timing_changed;

	v4l2_dbg(1, debug, sd, "%s: in\n", __func__);
	if (!tx_5v_power_present(sd)) {
		v4l2_dbg(2, debug, sd, "%s: HDMI pull out, return!\n", __func__);
		lt8619c->nosignal = true;
		return;
	}

	mutex_lock(&lt8619c->confctl_mutex);
	lt8619c_get_detected_timings(sd, &timings);
	is_supported_res = lt8619c_rcv_supported_res(sd, &timings);
	is_timing_changed = lt8619c_timing_changed(sd, &timings);
	v4l2_dbg(2, debug, sd,
		"%s: is_supported_res: %d, is_timing_changed: %d\n",
		__func__, is_supported_res, is_timing_changed);

	if (!is_supported_res) {
		lt8619c->nosignal = true;
		v4l2_dbg(1, debug, sd, "%s: no supported res, cfg as nosignal!\n",
				__func__);
	}

	if (is_supported_res && is_timing_changed) {
		lt8619c_config_all(sd);
		lt8619c->nosignal = false;
	}
	mutex_unlock(&lt8619c->confctl_mutex);

	schedule_delayed_work(&lt8619c->delayed_work_monitor_resolution, HZ);
}

static void lt8619c_load_hdcpkey(struct v4l2_subdev *sd)
{
	struct lt8619c *lt8619c = to_lt8619c(sd);
	int wait_cnt = 5;
	u32 val;

	regmap_write(lt8619c->reg_map, BANK_REG, BANK_80);
	regmap_write(lt8619c->reg_map, 0xb2, 0x50);
	regmap_write(lt8619c->reg_map, 0xa3, 0x77);
	while (wait_cnt) {
		usleep_range(50*1000, 50*1000);
		regmap_read(lt8619c->reg_map, 0xc0, &val);
		if (val & 0x8)
			break;
		wait_cnt--;
	}

	regmap_write(lt8619c->reg_map, 0xb2, 0xd0);
	regmap_write(lt8619c->reg_map, 0xa3, 0x57);
	if (val & 0x8)
		v4l2_info(sd, "load hdcp key success!\n");
	else
		v4l2_err(sd, "load hdcp key failed!\n");
}

static void lt8619c_set_hdmi_hdcp(struct v4l2_subdev *sd, bool enable)
{
	v4l2_dbg(2, debug, sd, "%s: %sable\n", __func__, enable ? "en" : "dis");

	if (enable)
		lt8619c_load_hdcpkey(sd);
	else
		v4l2_info(sd, "disable hdcp function!\n");
}

static void lt8619c_mode_config(struct v4l2_subdev *sd)
{
	struct lt8619c *lt8619c = to_lt8619c(sd);

	regmap_write(lt8619c->reg_map, BANK_REG, BANK_80);
	regmap_update_bits(lt8619c->reg_map, 0x2c, BIT(5) | BIT(4), BIT(5) | BIT(4));

	regmap_write(lt8619c->reg_map, BANK_REG, BANK_60);
	regmap_write(lt8619c->reg_map, 0x80, CLK_SRC);
	regmap_write(lt8619c->reg_map, 0x89, REF_RESISTANCE);
	regmap_write(lt8619c->reg_map, 0x8b, 0x90);
	/* Turn off BT output */
	regmap_write(lt8619c->reg_map, 0xa8, 0x07);
	/* enable PLL detect */
	regmap_write(lt8619c->reg_map, 0x04, 0xf2);

	if (lt8619c->BT656_double_clk_en) {
		regmap_write(lt8619c->reg_map, 0x96, 0x71);
		regmap_write(lt8619c->reg_map, 0xa0, 0x51);
		regmap_write(lt8619c->reg_map, 0xa3, 0x44);
		regmap_write(lt8619c->reg_map, 0xa2, 0x20);
	} else {
		regmap_write(lt8619c->reg_map, 0x96, 0x71);
		regmap_write(lt8619c->reg_map, 0xa0, 0x50);
		regmap_write(lt8619c->reg_map, 0xa3, 0x44);
		regmap_write(lt8619c->reg_map, 0xa2, 0x20);
	}
	regmap_update_bits(lt8619c->reg_map, 0x60, OUTPUT_MODE_MASK,
			lt8619c->yuv_output_mode);

	if (lt8619c->clk_ddrmode_en == 1)
		regmap_write(lt8619c->reg_map, 0xa4, 0x14);
	else
		regmap_write(lt8619c->reg_map, 0xa4, 0x10);

	/* Vblank change reference EAV flag. */
	regmap_write(lt8619c->reg_map, 0x6f, 0x04);

	v4l2_dbg(1, debug, sd, "%s: output mode:%s, clk ddrmode en:%d\n",
		__func__, (lt8619c->yuv_output_mode == BT656_OUTPUT) ? "BT656" :
		"BT1120", lt8619c->clk_ddrmode_en);
}

static void lt8619c_set_hpd(struct v4l2_subdev *sd, int en)
{
	struct lt8619c *lt8619c = to_lt8619c(sd);
	int level;

	v4l2_dbg(2, debug, sd, "%s: %d\n", __func__, en);

	level = lt8619c->hpd_output_inverted ? !en : en;
	regmap_write(lt8619c->reg_map, BANK_REG, BANK_80);
	if (level)
		regmap_update_bits(lt8619c->reg_map, 0x06, BIT(3), BIT(3));
	else
		regmap_update_bits(lt8619c->reg_map, 0x06, BIT(3), 0);
}

static void lt8619c_write_edid(struct v4l2_subdev *sd,
				struct v4l2_subdev_edid *edid)
{
	int i;
	struct lt8619c *lt8619c = to_lt8619c(sd);
	u32 edid_len = edid->blocks * EDID_BLOCK_SIZE;

	regmap_write(lt8619c->reg_map, BANK_REG, BANK_80);
	/* Enable EDID shadow operation */
	regmap_write(lt8619c->reg_map, 0x8e, 0x07);
	/* EDID data write start address */
	regmap_write(lt8619c->reg_map, 0x8f, 0x00);

	for (i = 0; i < edid_len; i++)
		regmap_write(lt8619c->reg_map, 0x90, edid->edid[i]);

	regmap_write(lt8619c->reg_map, 0x8e, 0x02);
}

static void lt8619c_read_edid(struct v4l2_subdev *sd, u8 *edid, u32 len)
{
	struct lt8619c *lt8619c = to_lt8619c(sd);
	int i;
	u32 val;

	regmap_write(lt8619c->reg_map, BANK_REG, BANK_80);
	/* Enable EDID shadow operation */
	regmap_write(lt8619c->reg_map, 0x8e, 0x07);
	/* EDID data write start address */
	regmap_write(lt8619c->reg_map, 0x8f, 0x00);
	for (i = 0; i < len; i++) {
		regmap_read(lt8619c->reg_map, 0x90, &val);
		edid[i] = val;
	}
	regmap_write(lt8619c->reg_map, 0x8e, 0x02);
}

static int lt8619c_s_ctrl_detect_tx_5v(struct v4l2_subdev *sd)
{
	struct lt8619c *lt8619c = to_lt8619c(sd);

	return v4l2_ctrl_s_ctrl(lt8619c->detect_tx_5v_ctrl,
			tx_5v_power_present(sd));
}

static int lt8619c_update_controls(struct v4l2_subdev *sd)
{
	int ret = 0;

	ret = lt8619c_s_ctrl_detect_tx_5v(sd);

	return ret;
}

static void enable_stream(struct v4l2_subdev *sd, bool enable)
{
	if (enable)
		v4l2_info(sd, "%s: stream on!\n", __func__);
	else
		v4l2_info(sd, "%s: stream off!\n", __func__);
}

static void lt8619c_set_bt_tx_timing(struct v4l2_subdev *sd)
{
	struct lt8619c *lt8619c = to_lt8619c(sd);
	struct v4l2_dv_timings timings;
	struct v4l2_bt_timings *bt = &timings.bt;
	u32 h_offset, v_offset, v_blank, htotal, vtotal;
	u32 hact, hfp, hbp, hs, vact, vfp, vbp, vs;
	u32 double_cnt = 1;

	/* read timing from HDMI RX */
	lt8619c_get_detected_timings(sd, &timings);

	hact = bt->width;
	vact = bt->height;
	hfp = bt->hfrontporch;
	hs = bt->hsync;
	hbp = bt->hbackporch;
	vfp = bt->vfrontporch;
	vs = bt->vsync;
	vbp = bt->vbackporch;
	htotal = hs + hbp + hact + hfp;
	vtotal = vs + vbp + vact + vfp;
	h_offset = hbp + hs;
	v_offset = vbp + vs;
	v_blank = vtotal - vact;

	if (bt->interlaced == V4L2_DV_INTERLACED) {
		/* already *2 in lt8619c_get_detected_timings */
		vact /= 2;
		double_cnt = 2;
		regmap_update_bits(lt8619c->reg_map, 0x60, IP_SEL_MASK,
				INTERLACE_INDICATOR);
	}

	vact = vact * double_cnt;
	vtotal = vtotal * double_cnt;

	v4l2_dbg(2, debug, sd,
		"%s: act:%dx%d, total:%dx%d, h_offset:%d, v_offset:%d, v_blank:%d\n",
		__func__, hact, vact, htotal, vtotal, h_offset, v_offset, v_blank);
	v4l2_dbg(2, debug, sd,
		"%s: hfp:%d, hs:%d, hbp:%d, vfp:%d, vs:%d, vbp:%d\n",
		__func__, hfp, hs, hbp, vfp, vs, vbp);

	/* write timing to BT TX */
	regmap_write(lt8619c->reg_map, BANK_REG, BANK_60);
	regmap_write(lt8619c->reg_map, 0x61, (h_offset >> 8) & 0xff);
	regmap_write(lt8619c->reg_map, 0x62, h_offset & 0xff);
	regmap_write(lt8619c->reg_map, 0x63, (hact >> 8) & 0xff);
	regmap_write(lt8619c->reg_map, 0x64, hact & 0xff);
	regmap_write(lt8619c->reg_map, 0x65, (htotal >> 8) & 0xff);
	regmap_write(lt8619c->reg_map, 0x66, htotal & 0xff);
	regmap_write(lt8619c->reg_map, 0x67, v_offset & 0xff);
	regmap_write(lt8619c->reg_map, 0x68, v_blank & 0xff);
	regmap_write(lt8619c->reg_map, 0x69, (vact >> 8) & 0xff);
	regmap_write(lt8619c->reg_map, 0x6a, vact & 0xff);
	regmap_write(lt8619c->reg_map, 0x6b, (vtotal >> 8) & 0xff);
	regmap_write(lt8619c->reg_map, 0x6c, vtotal & 0xff);
}

static void lt8619c_power_on(struct lt8619c *lt8619c)
{
	if (lt8619c->power_gpio) {
		gpiod_set_value(lt8619c->power_gpio, 1);
		usleep_range(1000, 1100);
	}

	if (lt8619c->reset_gpio) {
		gpiod_set_value(lt8619c->reset_gpio, 1);
		usleep_range(100*1000, 110*1000);
		gpiod_set_value(lt8619c->reset_gpio, 0);
		usleep_range(50*1000, 50*1000);
	}
}

static void lt8619c_wait_for_signal_stable(struct v4l2_subdev *sd)
{
	struct lt8619c *lt8619c = to_lt8619c(sd);
	int i;
	u32 val;

	regmap_write(lt8619c->reg_map, BANK_REG, BANK_80);
	for (i = 0; i < WAIT_MAX_TIMES; i++) {
		usleep_range(100*1000, 110*1000);
		regmap_read(lt8619c->reg_map, 0x43, &val);
		if (val & 0x80)
			break;
	}

	if (val & 0x80)
		v4l2_info(sd, "tmds clk det success, wait cnt:%d!\n", i);
	else
		v4l2_err(sd, "tmds clk det failed!\n");

	for (i = 0; i < WAIT_MAX_TIMES; i++) {
		usleep_range(100*1000, 110*1000);
		regmap_read(lt8619c->reg_map, 0x13, &val);
		if (val & 0x01)
			break;
	}

	if (val & 0x01)
		v4l2_info(sd, "Hsync stable, wait cnt:%d!\n", i);
	else
		v4l2_err(sd, "Hsync unstable!\n");

	/* reset HDMI RX logic */
	regmap_write(lt8619c->reg_map, BANK_REG, BANK_60);
	regmap_write(lt8619c->reg_map, 0x09, 0x7f);
	usleep_range(10*1000, 11*1000);
	regmap_write(lt8619c->reg_map, 0x09, 0xff);
	usleep_range(100*1000, 110*1000);

	/* reset video check logic */
	regmap_write(lt8619c->reg_map, 0x0c, 0xfb);
	usleep_range(10*1000, 11*1000);
	regmap_write(lt8619c->reg_map, 0x0c, 0xff);
	usleep_range(100*1000, 110*1000);
}

static void LVDSPLL_Lock_Det(struct v4l2_subdev *sd)
{
	int temp = 0;
	u32 val;
	struct lt8619c *lt8619c = to_lt8619c(sd);

	regmap_write(lt8619c->reg_map, BANK_REG, BANK_60);
	regmap_write(lt8619c->reg_map, 0x0e, 0xfd);
	usleep_range(5*1000, 5*1000);
	regmap_write(lt8619c->reg_map, 0x0e, 0xff);
	usleep_range(100*1000, 100*1000);

	regmap_write(lt8619c->reg_map, BANK_REG, BANK_80);
	regmap_read(lt8619c->reg_map, 0x87, &val);
	while ((val & 0x20) == 0x00) {
		regmap_write(lt8619c->reg_map, BANK_REG, BANK_60);
		regmap_write(lt8619c->reg_map, 0x0e, 0xfd);
		usleep_range(5*1000, 5*1000);
		regmap_write(lt8619c->reg_map, 0x0e, 0xff);

		regmap_write(lt8619c->reg_map, BANK_REG, BANK_80);
		regmap_read(lt8619c->reg_map, 0x87, &val);

		temp++;
		if (temp > 3) {
			v4l2_err(sd, "lvds pll lock det failed!\n");
			break;
		}
	}
}

static void LT8619C_phase_config(struct v4l2_subdev *sd)
{
	u32 i, val;
	int start = -1;
	int end = -1;
	u32 bt_clk_lag  = 0;
	struct lt8619c *lt8619c = to_lt8619c(sd);

	regmap_write(lt8619c->reg_map, BANK_REG, BANK_80);
	regmap_read(lt8619c->reg_map, 0x87, &val);
	while ((val & 0x20) == 0x00) {
		regmap_write(lt8619c->reg_map, BANK_REG, BANK_60);
		regmap_write(lt8619c->reg_map, 0x0e, 0xfd);
		usleep_range(5*1000, 5*1000);
		regmap_write(lt8619c->reg_map, 0x0e, 0xff);

		regmap_write(lt8619c->reg_map, 0x0a, 0x3f);
		usleep_range(5*1000, 5*1000);
		regmap_write(lt8619c->reg_map, 0x0a, 0x7f);
		usleep_range(100*1000, 100*1000);

		regmap_write(lt8619c->reg_map, BANK_REG, BANK_80);
		regmap_read(lt8619c->reg_map, 0x87, &val);
	}

	for (i = 0; i < ARRAY_SIZE(phase_num); i++) {
		regmap_write(lt8619c->reg_map, BANK_REG, BANK_60);
		regmap_write(lt8619c->reg_map, 0xa2, phase_num[i]);
		usleep_range(50*1000, 50*1000);
		regmap_read(lt8619c->reg_map, 0x91, &val);

		if (val == 0x05) {
			bt_clk_lag = 1;
			break;
		} else if (val == 0x01) {
			if (start == -1)
				start = i;

			end = i;
		}
	}

	v4l2_info(sd, "%s: BT_clk_lag:%d, start:%d, end:%d!\n", __func__,
			bt_clk_lag, start, end);
	if (bt_clk_lag) {
		regmap_write(lt8619c->reg_map, 0xa2, phase_num[i]);
	} else {
		if ((start != -1) && (end != -1) && (end >= start))
			regmap_write(lt8619c->reg_map, 0xa2,
					phase_num[start + (end - start) / 2]);
		else
			regmap_write(lt8619c->reg_map, 0xa2,
					phase_num[ARRAY_SIZE(phase_num) - 1]);
	}

	/* Turn on BT output */
	regmap_write(lt8619c->reg_map, 0xa8, 0x0f);
}

static void sync_polarity_config(struct v4l2_subdev *sd)
{
	struct lt8619c *lt8619c = to_lt8619c(sd);
	u32 val, adj;

	if (lt8619c->bt_tx_sync_pol == BT_TX_SYNC_POSITIVE) {
		v4l2_info(sd, "%s: cfg h_vsync pol: POSITIVE\n", __func__);
		regmap_write(lt8619c->reg_map, BANK_REG, BANK_60);
		regmap_update_bits(lt8619c->reg_map, 0x60, SYNC_POL_MASK,
				BT_TX_SYNC_POSITIVE);
		regmap_write(lt8619c->reg_map, BANK_REG, BANK_80);
		regmap_read(lt8619c->reg_map, 0x17, &val);
		regmap_read(lt8619c->reg_map, 0x05, &adj);
		if ((val & RGOD_VID_VSPOL) != RGOD_VID_VSPOL) {
			adj ^= RGD_VS_POL_ADJ_MASK;
			regmap_update_bits(lt8619c->reg_map, 0x05,
					RGD_VS_POL_ADJ_MASK, adj);
		}

		if ((val & RGOD_VID_HSPOL) != RGOD_VID_HSPOL) {
			adj ^= RGD_HS_POL_ADJ_MASK;
			regmap_update_bits(lt8619c->reg_map, 0x05,
					RGD_HS_POL_ADJ_MASK, adj);
		}
	} else {
		v4l2_info(sd, "%s: cfg h_vsync pol: NEGATIVE\n", __func__);
		regmap_write(lt8619c->reg_map, BANK_REG, BANK_60);
		regmap_update_bits(lt8619c->reg_map, 0x60, SYNC_POL_MASK,
				BT_TX_SYNC_NEGATIVE);
		regmap_write(lt8619c->reg_map, BANK_REG, BANK_80);
		regmap_read(lt8619c->reg_map, 0x17, &val);
		regmap_read(lt8619c->reg_map, 0x05, &adj);
		if ((val & RGOD_VID_VSPOL) == RGOD_VID_VSPOL) {
			adj ^= RGD_VS_POL_ADJ_MASK;
			regmap_update_bits(lt8619c->reg_map, 0x05,
					RGD_VS_POL_ADJ_MASK, adj);
		}

		if ((val & RGOD_VID_HSPOL) == RGOD_VID_HSPOL) {
			adj ^= RGD_HS_POL_ADJ_MASK;
			regmap_update_bits(lt8619c->reg_map, 0x05,
					RGD_HS_POL_ADJ_MASK, adj);
		}
	}
}

static void lt8619c_yuv_config(struct v4l2_subdev *sd)
{
	struct lt8619c *lt8619c = to_lt8619c(sd);
	u32 val, colorspace;

	sync_polarity_config(sd);

	/* softrest BT TX module */
	regmap_write(lt8619c->reg_map, BANK_REG, BANK_60);
	regmap_update_bits(lt8619c->reg_map, 0x0d, BIT(1) | BIT(0), 0);
	usleep_range(10*1000, 10*1000);
	regmap_update_bits(lt8619c->reg_map, 0x0d, BIT(1) | BIT(0), BIT(1) | BIT(0));

	/* ColorSpace convert */
	regmap_write(lt8619c->reg_map, BANK_REG, BANK_80);
	regmap_read(lt8619c->reg_map, 0x71, &val);
	colorspace = (val & 0x60) >> 5;
	if (colorspace == 2) {
		/* YCbCr444 convert YCbCr422 enable */
		regmap_write(lt8619c->reg_map, BANK_REG, BANK_60);
		regmap_write(lt8619c->reg_map, 0x07, 0xf0);
		regmap_write(lt8619c->reg_map, 0x52, 0x02 +
				lt8619c->cp_convert_mode);
		v4l2_info(sd, "%s: colorspace: yuv444\n", __func__);
	} else if (colorspace == 1) {
		/* yuv422 */
		regmap_write(lt8619c->reg_map, BANK_REG, BANK_60);
		regmap_write(lt8619c->reg_map, 0x07, 0x80);
		regmap_write(lt8619c->reg_map, 0x52, 0x00);
		v4l2_info(sd, "%s: colorspace: yuv222\n", __func__);
	} else {
		/* RGB convert YCbCr422 enable */
		regmap_write(lt8619c->reg_map, BANK_REG, BANK_60);
		regmap_write(lt8619c->reg_map, 0x07, 0xf0);
		regmap_write(lt8619c->reg_map, 0x52, 0x0a +
					lt8619c->cp_convert_mode);
		v4l2_info(sd, "%s: colorspace: RGB\n", __func__);
	}

	lt8619c_set_bt_tx_timing(sd);
	regmap_write(lt8619c->reg_map, BANK_REG, BANK_60);
	regmap_write(lt8619c->reg_map, 0x6d, lt8619c->yc_swap);
	regmap_write(lt8619c->reg_map, 0x6e, lt8619c->yuv_colordepth);

	/* LVDS PLL soft reset */
	regmap_update_bits(lt8619c->reg_map, 0x0e, BIT(1), 0);
	usleep_range(50*1000, 50*1000);
	regmap_update_bits(lt8619c->reg_map, 0x0e, BIT(1), BIT(1));

	/* BT TX controller and afifo soft reset */
	regmap_update_bits(lt8619c->reg_map, 0x0d, BIT(1) | BIT(0), 0);
	usleep_range(50*1000, 50*1000);
	regmap_update_bits(lt8619c->reg_map, 0x0d, BIT(1) | BIT(0), BIT(1) | BIT(0));
}

static void lt8619c_initial_setup(struct v4l2_subdev *sd)
{
	static struct v4l2_dv_timings default_timing =
		V4L2_DV_BT_CEA_640X480P59_94;
	struct lt8619c *lt8619c = to_lt8619c(sd);
	struct v4l2_subdev_edid def_edid;

	def_edid.pad = 0;
	def_edid.start_block = 0;
	def_edid.blocks = 2;
	def_edid.edid = edid_init_data;
	lt8619c->enable_hdcp = false;
	lt8619c->cp_convert_mode = CP_CONVERT_MODE;
	lt8619c->yuv_colordepth = YUV_COLORDEPTH;
	lt8619c->bt_tx_sync_pol = BT_TX_SYNC_POL;

	if (lt8619c->yuv_output_mode == BT656_OUTPUT) {
		lt8619c->yc_swap = YC_SWAP_DIS;
		lt8619c->BT656_double_clk_en = true;
	} else {
		lt8619c->yc_swap = YC_SWAP_EN;
		lt8619c->BT656_double_clk_en = false;
	}

	lt8619c_set_hpd(sd, 0);
	lt8619c_write_edid(sd, &def_edid);
	lt8619c->edid_blocks_written = def_edid.blocks;
	lt8619c_set_hdmi_hdcp(sd, lt8619c->enable_hdcp);
	lt8619c_mode_config(sd);

	if (tx_5v_power_present(sd)) {
		lt8619c_set_hpd(sd, 1);
		lt8619c_config_all(sd);
		/* monitor resolution after 100ms */
		schedule_delayed_work(&lt8619c->delayed_work_monitor_resolution,
				HZ / 10);
	} else {
		lt8619c_s_dv_timings(sd, &default_timing);
	}

	v4l2_dbg(1, debug, sd, "%s: init ok\n", __func__);
}

static void lt8619c_format_change(struct v4l2_subdev *sd)
{
	struct lt8619c *lt8619c = to_lt8619c(sd);
	struct v4l2_dv_timings timings;
	const struct v4l2_event lt8619c_ev_fmt = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
	};

	lt8619c_get_detected_timings(sd, &timings);
	if (!v4l2_match_dv_timings(&lt8619c->timings, &timings, 0, false)) {
		/* automatically set timing rather than set by userspace */
		lt8619c_s_dv_timings(sd, &timings);
		v4l2_print_dv_timings(sd->name,
			"lt8619c_format_change: New format: ", &timings, false);

	}

	if (sd->devnode)
		v4l2_subdev_notify_event(sd, &lt8619c_ev_fmt);
}

static bool lt8619c_timing_changed(struct v4l2_subdev *sd,
		struct v4l2_dv_timings *timings)
{
	struct lt8619c *lt8619c = to_lt8619c(sd);
	struct v4l2_bt_timings *new_bt = &timings->bt;
	struct v4l2_bt_timings *bt = &lt8619c->timings.bt;

	if ((bt->width != new_bt->width) |
	    (bt->height != new_bt->height) |
	    (abs(bt->hfrontporch - new_bt->hfrontporch) > 1) |
	    (abs(bt->hsync - new_bt->hsync) > 1) |
	    (abs(bt->hbackporch - new_bt->hbackporch) > 1) |
	    (abs(bt->vfrontporch - new_bt->vfrontporch) > 1) |
	    (abs(bt->vsync - new_bt->vsync) > 1) |
	    (abs(bt->vbackporch - new_bt->vbackporch) > 1) |
	    (abs(bt->pixelclock - new_bt->pixelclock) > 5000)) {
		v4l2_info(sd, "%s: timing changed!\n", __func__);
		return true;
	}

	return false;
}

static bool lt8619c_rcv_supported_res(struct v4l2_subdev *sd,
					struct v4l2_dv_timings *timings)
{
	u32 i;
	u32 hact, vact, htotal, vtotal;
	struct v4l2_bt_timings *bt = &timings->bt;

	hact = bt->width;
	vact = bt->height;
	htotal = bt->hsync + bt->hbackporch + hact + bt->hfrontporch;
	vtotal = bt->vsync + bt->vbackporch + vact + bt->vfrontporch;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		if ((supported_modes[i].width == hact) &&
		    (supported_modes[i].height == vact)) {
			break;
		}
	}

	if (i == ARRAY_SIZE(supported_modes)) {
		v4l2_err(sd, "%s do not support res act: %dx%d, total: %dx%d\n",
				__func__, hact, vact, htotal, vtotal);
		return false;
	}

	if (bt->pixelclock < 25000000) {
		v4l2_err(sd, "%s pixclk: %llu, err!\n", __func__, bt->pixelclock);
		return false;
	}

	return true;
}

static int lt8619c_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
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

static int lt8619c_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	*status = 0;
	*status |= no_signal(sd) ? V4L2_IN_ST_NO_SIGNAL : 0;

	v4l2_dbg(1, debug, sd, "%s: status = 0x%x\n", __func__, *status);

	return 0;
}

static int lt8619c_s_dv_timings(struct v4l2_subdev *sd,
				 struct v4l2_dv_timings *timings)
{
	struct lt8619c *lt8619c = to_lt8619c(sd);

	if (!timings)
		return -EINVAL;

	if (debug)
		v4l2_print_dv_timings(sd->name, "lt8619c_s_dv_timings: ",
				timings, false);

	if (v4l2_match_dv_timings(&lt8619c->timings, timings, 0, false)) {
		v4l2_dbg(1, debug, sd, "%s: no change\n", __func__);
		return 0;
	}

	if (!v4l2_valid_dv_timings(timings, &lt8619c_timings_cap, NULL, NULL)) {
		v4l2_dbg(1, debug, sd, "%s: timings out of range\n", __func__);
		return -ERANGE;
	}

	lt8619c->timings = *timings;
	enable_stream(sd, false);

	return 0;
}

static int lt8619c_g_dv_timings(struct v4l2_subdev *sd,
				 struct v4l2_dv_timings *timings)
{
	struct lt8619c *lt8619c = to_lt8619c(sd);

	*timings = lt8619c->timings;

	return 0;
}

static int lt8619c_enum_dv_timings(struct v4l2_subdev *sd,
				    struct v4l2_enum_dv_timings *timings)
{
	if (timings->pad != 0)
		return -EINVAL;

	return v4l2_enum_dv_timings_cap(timings, &lt8619c_timings_cap, NULL, NULL);
}

static int lt8619c_query_dv_timings(struct v4l2_subdev *sd,
		struct v4l2_dv_timings *timings)
{
	int ret;
	struct lt8619c *lt8619c = to_lt8619c(sd);

	mutex_lock(&lt8619c->confctl_mutex);
	ret = lt8619c_get_detected_timings(sd, timings);
	mutex_unlock(&lt8619c->confctl_mutex);
	if (ret)
		return ret;

	if (debug)
		v4l2_print_dv_timings(sd->name, "lt8619c_query_dv_timings: ",
				timings, false);

	if (!v4l2_valid_dv_timings(timings, &lt8619c_timings_cap, NULL, NULL)) {
		v4l2_dbg(1, debug, sd, "%s: timings out of range\n", __func__);
		return -ERANGE;
	}

	return 0;
}

static int lt8619c_dv_timings_cap(struct v4l2_subdev *sd,
		struct v4l2_dv_timings_cap *cap)
{
	if (cap->pad != 0)
		return -EINVAL;

	*cap = lt8619c_timings_cap;

	return 0;
}

static int lt8619c_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
			     struct v4l2_mbus_config *cfg)
{
	struct lt8619c *lt8619c = to_lt8619c(sd);

	cfg->type = V4L2_MBUS_BT656;
	if (lt8619c->clk_ddrmode_en) {
		cfg->flags = RKMODULE_CAMERA_BT656_CHANNELS |
			V4L2_MBUS_PCLK_SAMPLE_RISING |
			V4L2_MBUS_PCLK_SAMPLE_FALLING;
	} else {
		cfg->flags = RKMODULE_CAMERA_BT656_CHANNELS |
			V4L2_MBUS_PCLK_SAMPLE_RISING;
	}

	return 0;
}

static int lt8619c_s_stream(struct v4l2_subdev *sd, int enable)
{
	enable_stream(sd, enable);

	return 0;
}

static int lt8619c_enum_mbus_code(struct v4l2_subdev *sd,
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

static int lt8619c_get_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret = -1;
	struct lt8619c *lt8619c = container_of(ctrl->handler, struct lt8619c, hdl);
	struct v4l2_subdev *sd = &(lt8619c->sd);

	if (ctrl->id == V4L2_CID_DV_RX_POWER_PRESENT) {
		ret = tx_5v_power_present(sd);
		*ctrl->p_new.p_s32 = ret;
	}

	return ret;
}

static int lt8619c_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	v4l2_dbg(1, debug, sd, "%s:\n", __func__);

	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_UYVY8_2X8)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int lt8619c_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fie->code != MEDIA_BUS_FMT_UYVY8_2X8)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	return 0;
}

static int lt8619c_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct lt8619c *lt8619c = to_lt8619c(sd);
	struct v4l2_bt_timings *bt = &(lt8619c->timings.bt);

	mutex_lock(&lt8619c->confctl_mutex);
	format->format.code = lt8619c->mbus_fmt_code;
	format->format.width = lt8619c->timings.bt.width;
	format->format.height = lt8619c->timings.bt.height;
	format->format.colorspace = V4L2_COLORSPACE_SRGB;
	if (bt->interlaced == V4L2_DV_INTERLACED)
		format->format.field = V4L2_FIELD_INTERLACED;
	else
		format->format.field = V4L2_FIELD_NONE;
	mutex_unlock(&lt8619c->confctl_mutex);

	v4l2_dbg(2, debug, sd, "fmt code:%d, w:%d, h:%d, field:%s, cosp:%d\n",
			format->format.code,
			format->format.width,
			format->format.height,
			(format->format.field == V4L2_FIELD_INTERLACED) ?
				"I" : "P",
			format->format.colorspace);

	return 0;
}

static int lt8619c_get_reso_dist(const struct lt8619c_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct lt8619c_mode *
lt8619c_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = lt8619c_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int lt8619c_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct lt8619c *lt8619c = to_lt8619c(sd);
	const struct lt8619c_mode *mode;

	/* is overwritten by get_fmt */
	u32 code = format->format.code;
	int ret = lt8619c_get_fmt(sd, cfg, format);

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

	lt8619c->mbus_fmt_code = format->format.code;
	mode = lt8619c_find_best_fit(format);
	lt8619c->cur_mode = mode;

	enable_stream(sd, false);

	return 0;
}

static int lt8619c_g_edid(struct v4l2_subdev *sd,
		struct v4l2_subdev_edid *edid)
{
	struct lt8619c *lt8619c = to_lt8619c(sd);

	memset(edid->reserved, 0, sizeof(edid->reserved));

	if (edid->pad != 0)
		return -EINVAL;

	if (edid->start_block == 0 && edid->blocks == 0) {
		edid->blocks = lt8619c->edid_blocks_written;
		return 0;
	}

	if (lt8619c->edid_blocks_written == 0)
		return -ENODATA;

	if (edid->start_block >= lt8619c->edid_blocks_written ||
			edid->blocks == 0)
		return -EINVAL;

	if (edid->start_block + edid->blocks > lt8619c->edid_blocks_written)
		edid->blocks = lt8619c->edid_blocks_written - edid->start_block;

	lt8619c_read_edid(sd, edid->edid, edid->blocks * EDID_BLOCK_SIZE);

	return 0;
}

static int lt8619c_s_edid(struct v4l2_subdev *sd,
				struct v4l2_subdev_edid *edid)
{
	struct lt8619c *lt8619c = to_lt8619c(sd);

	v4l2_dbg(2, debug, sd, "%s, pad %d, start block %d, blocks %d\n",
		 __func__, edid->pad, edid->start_block, edid->blocks);

	memset(edid->reserved, 0, sizeof(edid->reserved));

	if (edid->pad != 0)
		return -EINVAL;

	if (edid->start_block != 0)
		return -EINVAL;

	if (edid->blocks > EDID_NUM_BLOCKS_MAX) {
		edid->blocks = EDID_NUM_BLOCKS_MAX;
		return -E2BIG;
	}

	lt8619c_set_hpd(sd, 0);

	if (edid->blocks == 0) {
		lt8619c->edid_blocks_written = 0;
		return 0;
	}

	lt8619c_write_edid(sd, edid);
	lt8619c->edid_blocks_written = edid->blocks;

	if (tx_5v_power_present(sd))
		lt8619c_set_hpd(sd, 1);

	return 0;
}

static int lt8619c_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct lt8619c *lt8619c = to_lt8619c(sd);
	const struct lt8619c_mode *mode = lt8619c->cur_mode;

	mutex_lock(&lt8619c->confctl_mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&lt8619c->confctl_mutex);

	return 0;
}

static int lt8619c_querystd(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	struct lt8619c *lt8619c = to_lt8619c(sd);

	if (lt8619c->yuv_output_mode == BT656_OUTPUT)
		*std = V4L2_STD_PAL;
	else
		*std = V4L2_STD_ATSC;

	return 0;
}

static void lt8619c_get_module_inf(struct lt8619c *lt8619c,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, LT8619C_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, lt8619c->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, lt8619c->len_name, sizeof(inf->base.lens));
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int lt8619c_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct lt8619c *lt8619c = to_lt8619c(sd);
	struct v4l2_bt_timings *bt = &(lt8619c->timings.bt);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct lt8619c_mode *def_mode = &supported_modes[0];

	mutex_lock(&lt8619c->confctl_mutex);
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_UYVY8_2X8;
	if (bt->interlaced == V4L2_DV_INTERLACED)
		try_fmt->field = V4L2_FIELD_INTERLACED;
	else
		try_fmt->field = V4L2_FIELD_NONE;
	mutex_unlock(&lt8619c->confctl_mutex);

	return 0;
}
#endif

static long lt8619c_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct lt8619c *lt8619c = to_lt8619c(sd);
	long ret = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		lt8619c_get_module_inf(lt8619c, (struct rkmodule_inf *)arg);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long lt8619c_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	long ret;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = lt8619c_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(inf);
		break;

	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static const struct v4l2_ctrl_ops lt8619c_ctrl_ops = {
	.g_volatile_ctrl = lt8619c_get_ctrl,
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops lt8619c_subdev_internal_ops = {
	.open = lt8619c_open,
};
#endif

static const struct v4l2_subdev_core_ops lt8619c_core_ops = {
	.subscribe_event = lt8619c_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
	.ioctl = lt8619c_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = lt8619c_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops lt8619c_video_ops = {
	.g_input_status = lt8619c_g_input_status,
	.s_dv_timings = lt8619c_s_dv_timings,
	.g_dv_timings = lt8619c_g_dv_timings,
	.query_dv_timings = lt8619c_query_dv_timings,
	.s_stream = lt8619c_s_stream,
	.g_frame_interval = lt8619c_g_frame_interval,
	.querystd = lt8619c_querystd,
};

static const struct v4l2_subdev_pad_ops lt8619c_pad_ops = {
	.enum_mbus_code = lt8619c_enum_mbus_code,
	.enum_frame_size = lt8619c_enum_frame_sizes,
	.enum_frame_interval = lt8619c_enum_frame_interval,
	.set_fmt = lt8619c_set_fmt,
	.get_fmt = lt8619c_get_fmt,
	.get_edid = lt8619c_g_edid,
	.set_edid = lt8619c_s_edid,
	.enum_dv_timings = lt8619c_enum_dv_timings,
	.dv_timings_cap = lt8619c_dv_timings_cap,
	.get_mbus_config = lt8619c_g_mbus_config,
};

static const struct v4l2_subdev_ops lt8619c_ops = {
	.core = &lt8619c_core_ops,
	.video = &lt8619c_video_ops,
	.pad = &lt8619c_pad_ops,
};

static irqreturn_t plugin_detect_irq(int irq, void *dev_id)
{
	struct lt8619c *lt8619c = dev_id;
	struct v4l2_subdev *sd = &lt8619c->sd;

	/* enable hpd after 100ms */
	schedule_delayed_work(&lt8619c->delayed_work_enable_hotplug, HZ / 10);
	v4l2_dbg(2, debug, sd, "%s: plug change!\n", __func__);
	tx_5v_power_present(sd);

	return IRQ_HANDLED;
}

static int lt8619c_parse_of(struct lt8619c *lt8619c)
{
	struct device *dev = &lt8619c->i2c_client->dev;
	struct device_node *node = dev->of_node;
	int err;

	lt8619c->hpd_output_inverted = of_property_read_bool(node,
				"hpd-output-inverted");
	err = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &lt8619c->module_index);
	err |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &lt8619c->module_facing);
	err |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &lt8619c->module_name);
	err |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &lt8619c->len_name);
	if (err) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	lt8619c->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(lt8619c->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	err = clk_prepare_enable(lt8619c->xvclk);
	if (err) {
		dev_err(dev, "Failed! to enable xvclk\n");
		return err;
	}

	lt8619c->power_gpio = devm_gpiod_get_optional(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(lt8619c->power_gpio)) {
		dev_err(dev, "failed to get power gpio\n");
		err = PTR_ERR(lt8619c->power_gpio);
		goto disable_clk;
	}
	lt8619c->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(lt8619c->reset_gpio)) {
		dev_err(dev, "failed to get reset gpio\n");
		err = PTR_ERR(lt8619c->reset_gpio);
		goto disable_clk;
	}

	lt8619c->plugin_det_gpio = devm_gpiod_get_optional(dev, "plugin-det",
			GPIOD_IN);
	if (IS_ERR(lt8619c->plugin_det_gpio)) {
		dev_err(dev, "failed to get plugin_det gpio\n");
		err = PTR_ERR(lt8619c->plugin_det_gpio);
		goto disable_clk;
	}

	if (of_property_read_u32(node, RK_CAMERA_MODULE_DUAL_EDGE,
					&lt8619c->clk_ddrmode_en)) {
		lt8619c->clk_ddrmode_en = LT8619C_DEFAULT_DUAL_EDGE;
		dev_warn(dev, "can not get module %s from dts, use default(%d)!\n",
			RK_CAMERA_MODULE_DUAL_EDGE, LT8619C_DEFAULT_DUAL_EDGE);
	} else {
		dev_info(dev, "get module %s from dts, dual_edge(%d)!\n",
			RK_CAMERA_MODULE_DUAL_EDGE, lt8619c->clk_ddrmode_en);
	}

	if (of_property_read_u32(node, RK_CAMERA_MODULE_DVP_MODE,
					&lt8619c->yuv_output_mode)) {
		lt8619c->yuv_output_mode = LT8619C_DEFAULT_DVP_MODE;
		dev_warn(dev, "can not get module %s from dts, use default(BT1120)!\n",
					RK_CAMERA_MODULE_DVP_MODE);
	} else {
		dev_info(dev, "get module %s from dts, dvp mode(%s)!\n",
			RK_CAMERA_MODULE_DVP_MODE,
			(lt8619c->yuv_output_mode == BT656_OUTPUT) ? "BT656" : "BT1120");
	}

	return 0;

disable_clk:
	clk_disable_unprepare(lt8619c->xvclk);
	return err;
}

static int lt8619c_init_v4l2_ctrls(struct lt8619c *lt8619c)
{
	struct v4l2_subdev *sd;
	int ret;

	sd = &lt8619c->sd;
	ret = v4l2_ctrl_handler_init(&lt8619c->hdl, 2);
	if (ret)
		return ret;

	v4l2_ctrl_new_std(&lt8619c->hdl, NULL, V4L2_CID_PIXEL_RATE,
			  0, lt8619c_PIXEL_RATE, 1, lt8619c_PIXEL_RATE);
	lt8619c->detect_tx_5v_ctrl = v4l2_ctrl_new_std(&lt8619c->hdl,
			&lt8619c_ctrl_ops, V4L2_CID_DV_RX_POWER_PRESENT,
			0, 1, 0, 0);
	if (lt8619c->detect_tx_5v_ctrl)
		lt8619c->detect_tx_5v_ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	if (lt8619c->hdl.error) {
		ret = lt8619c->hdl.error;
		v4l2_err(sd, "cfg v4l2 ctrls failed! ret:%d\n", ret);
		return ret;
	}
	sd->ctrl_handler = &lt8619c->hdl;

	if (lt8619c_update_controls(sd)) {
		ret = -ENODEV;
		v4l2_err(sd, "update v4l2 ctrls failed! ret:%d\n", ret);
		return ret;
	}

	return 0;
}

static int lt8619c_check_chip_id(struct lt8619c *lt8619c)
{
	struct device *dev = &lt8619c->i2c_client->dev;
	u32 id_h, id_m, id_l;
	int ret;
	u32 chipid;

	regmap_write(lt8619c->reg_map, BANK_REG, BANK_60);
	ret  = regmap_read(lt8619c->reg_map, CHIPID_REG_H, &id_h);
	ret |= regmap_read(lt8619c->reg_map, CHIPID_REG_M, &id_m);
	ret |= regmap_read(lt8619c->reg_map, CHIPID_REG_L, &id_l);

	if (!ret) {
		chipid = (id_h << 16) | (id_m << 8) | id_l;
		if (chipid != LT8619C_CHIPID) {
			dev_err(dev,
				"check chipid failed, read id:%#x, we expect:%#x\n",
				chipid, LT8619C_CHIPID);
			ret = -1;
		}
	} else {
		dev_err(dev, "%s i2c trans failed!\n", __func__);
		ret = -1;
	}

	return ret;
}

static const struct regmap_range lt8619c_readable_ranges[] = {
	regmap_reg_range(0x00, 0xff),
};

static const struct regmap_access_table lt8619c_readable_table = {
	.yes_ranges     = lt8619c_readable_ranges,
	.n_yes_ranges   = ARRAY_SIZE(lt8619c_readable_ranges),
};

static const struct regmap_config lt8619c_hdmirx_regmap_cfg = {
	.name = "lt8619c",
	.reg_bits = 8,
	.val_bits = 8,
	.reg_stride = 1,
	.max_register = LT8619C_MAX_REGISTER,
	.reg_format_endian = REGMAP_ENDIAN_LITTLE,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.rd_table = &lt8619c_readable_table,
};

static int lt8619c_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct lt8619c *lt8619c;
	struct v4l2_subdev *sd;
	char facing[2];
	int err;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	lt8619c = devm_kzalloc(dev, sizeof(*lt8619c), GFP_KERNEL);
	if (!lt8619c)
		return -ENOMEM;

	sd = &lt8619c->sd;
	lt8619c->i2c_client = client;
	lt8619c->cur_mode = &supported_modes[0];
	lt8619c->mbus_fmt_code = MEDIA_BUS_FMT_UYVY8_2X8;

	err = lt8619c_parse_of(lt8619c);
	if (err)
		return err;

	mutex_init(&lt8619c->confctl_mutex);
	err = lt8619c_init_v4l2_ctrls(lt8619c);
	if (err)
		goto err_hdl;

	client->flags |= I2C_CLIENT_SCCB;
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	v4l2_i2c_subdev_init(sd, client, &lt8619c_ops);
	sd->internal_ops = &lt8619c_subdev_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
#endif

#if defined(CONFIG_MEDIA_CONTROLLER)
	lt8619c->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	err = media_entity_pads_init(&sd->entity, 1, &lt8619c->pad);
	if (err < 0) {
		v4l2_err(sd, "media entity init failed! err:%d\n", err);
		goto err_hdl;
	}
#endif

	lt8619c->reg_map = devm_regmap_init_i2c(client, &lt8619c_hdmirx_regmap_cfg);
	lt8619c_power_on(lt8619c);
	err = lt8619c_check_chip_id(lt8619c);
	if (err < 0)
		goto err_hdl;

	memset(facing, 0, sizeof(facing));
	if (strcmp(lt8619c->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 lt8619c->module_index, facing,
		 LT8619C_NAME, dev_name(sd->dev));
	err = v4l2_async_register_subdev_sensor_common(sd);
	if (err < 0) {
		v4l2_err(sd, "v4l2 register subdev failed! err:%d\n", err);
		goto err_subdev;
	}

	INIT_DELAYED_WORK(&lt8619c->delayed_work_enable_hotplug,
			lt8619c_delayed_work_enable_hotplug);
	INIT_DELAYED_WORK(&lt8619c->delayed_work_monitor_resolution,
			lt8619c_delayed_work_monitor_resolution);
	lt8619c_initial_setup(sd);

	lt8619c->plugin_irq = gpiod_to_irq(lt8619c->plugin_det_gpio);
	if (lt8619c->plugin_irq < 0) {
		dev_err(dev, "failed to get plugin det irq\n");
		err = lt8619c->plugin_irq;
		goto err_work_queues;
	}

	err = devm_request_threaded_irq(dev, lt8619c->plugin_irq, NULL,
			plugin_detect_irq, IRQF_TRIGGER_FALLING |
			IRQF_TRIGGER_RISING | IRQF_ONESHOT, "lt8619c", lt8619c);
	if (err) {
		dev_err(dev, "failed to register plugin det irq (%d)\n", err);
		goto err_work_queues;
	}

	err = v4l2_ctrl_handler_setup(sd->ctrl_handler);
	if (err) {
		v4l2_err(sd, "v4l2 ctrl handler setup failed! err:%d\n", err);
		goto err_work_queues;
	}

	v4l2_info(sd, "%s found @ 0x%x (%s)\n", client->name,
		  client->addr << 1, client->adapter->name);

	return 0;

err_work_queues:
	cancel_delayed_work(&lt8619c->delayed_work_enable_hotplug);
	cancel_delayed_work(&lt8619c->delayed_work_monitor_resolution);
err_subdev:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_hdl:
	v4l2_ctrl_handler_free(&lt8619c->hdl);
	mutex_destroy(&lt8619c->confctl_mutex);
	clk_disable_unprepare(lt8619c->xvclk);
	return err;
}

static int lt8619c_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct lt8619c *lt8619c = to_lt8619c(sd);

	cancel_delayed_work(&lt8619c->delayed_work_enable_hotplug);
	cancel_delayed_work(&lt8619c->delayed_work_monitor_resolution);
	v4l2_async_unregister_subdev(sd);
	v4l2_device_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&lt8619c->hdl);
	mutex_destroy(&lt8619c->confctl_mutex);
	clk_disable_unprepare(lt8619c->xvclk);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id lt8619c_of_match[] = {
	{ .compatible = "lontium,lt8619c" },
	{},
};
MODULE_DEVICE_TABLE(of, lt8619c_of_match);
#endif

static struct i2c_driver lt8619c_i2c_driver = {
	.driver = {
		.name = LT8619C_NAME,
		.of_match_table = of_match_ptr(lt8619c_of_match),
	},
	.probe		= &lt8619c_probe,
	.remove		= &lt8619c_remove,
};

static int __init lt8619c_driver_init(void)
{
	return i2c_add_driver(&lt8619c_i2c_driver);
}

static void __exit lt8619c_driver_exit(void)
{
	i2c_del_driver(&lt8619c_i2c_driver);
}

device_initcall_sync(lt8619c_driver_init);
module_exit(lt8619c_driver_exit);

MODULE_DESCRIPTION("Lontium LT8619C HDMI to BT656/BT1120 bridge driver");
MODULE_AUTHOR("Dingxian Wen <shawn.wen@rock-chips.com>");
MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Dingxian Wen <shawn.wen@rock-chips.com>
 * V0.0X01.0X00 first version.
 * V0.0X01.0X01 fix if plugin_gpio was not used.
 * V0.0X01.0X02 modify driver init level to late_initcall.
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
#include "lt6911uxc.h"

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x2)
#define LT6911UXC_NAME			"LT6911UXC"

#define LT6911UXC_LINK_FREQ_HIGH	400000000
#define LT6911UXC_LINK_FREQ_LOW		200000000
#define LT6911UXC_PIXEL_RATE		400000000

#define I2C_MAX_XFER_SIZE		128

#ifdef LT6911UXC_OUT_RGB
#define LT6911UXC_MEDIA_BUS_FMT		MEDIA_BUS_FMT_BGR888_1X24
#else
#define LT6911UXC_MEDIA_BUS_FMT		MEDIA_BUS_FMT_UYVY8_2X8
#endif

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-2)");

static const s64 link_freq_menu_items[] = {
	LT6911UXC_LINK_FREQ_HIGH,
	LT6911UXC_LINK_FREQ_LOW,
};

struct lt6911uxc {
	struct clk *xvclk;
	struct delayed_work delayed_work_enable_hotplug;
	struct delayed_work delayed_work_res_change;
	struct gpio_desc *hpd_ctl_gpio;
	struct gpio_desc *plugin_det_gpio;
	struct gpio_desc *power_gpio;
	struct gpio_desc *reset_gpio;
	struct i2c_client *i2c_client;
	struct media_pad pad;
	struct mutex confctl_mutex;
	struct v4l2_ctrl *audio_present_ctrl;
	struct v4l2_ctrl *audio_sampling_rate_ctrl;
	struct v4l2_ctrl *detect_tx_5v_ctrl;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl_handler hdl;
	struct v4l2_dv_timings timings;
	struct v4l2_fwnode_bus_mipi_csi2 bus;
	struct v4l2_subdev sd;
	const char *len_name;
	const char *module_facing;
	const char *module_name;
	const struct lt6911uxc_mode *cur_mode;
	bool enable_hdcp;
	bool nosignal;
	bool is_audio_present;
	int plugin_irq;
	u32 mbus_fmt_code;
	u32 module_index;
	u32 csi_lanes_in_use;
	u32 audio_sampling_rate;
};

struct lt6911uxc_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
};

static const struct v4l2_dv_timings_cap lt6911uxc_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	/* keep this initialization for compatibility with GCC < 4.4.6 */
	.reserved = { 0 },
	V4L2_INIT_BT_TIMINGS(1, 10000, 1, 10000, 0, 400000000,
			V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
			V4L2_DV_BT_STD_GTF | V4L2_DV_BT_STD_CVT,
			V4L2_DV_BT_CAP_PROGRESSIVE |
			V4L2_DV_BT_CAP_INTERLACED |
			V4L2_DV_BT_CAP_REDUCED_BLANKING |
			V4L2_DV_BT_CAP_CUSTOM)
};

static const struct lt6911uxc_mode supported_modes[] = {
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
		.height = 540,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
	}, {
		.width = 1440,
		.height = 240,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
	}, {
		.width = 1440,
		.height = 288,
		.max_fps = {
			.numerator = 10000,
			.denominator = 500000,
		},
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
	},
};

static void lt6911uxc_format_change(struct v4l2_subdev *sd);
static int lt6911uxc_s_ctrl_detect_tx_5v(struct v4l2_subdev *sd);
static int lt6911uxc_s_dv_timings(struct v4l2_subdev *sd,
				 struct v4l2_dv_timings *timings);

static inline struct lt6911uxc *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct lt6911uxc, sd);
}

static int i2c_rd(struct v4l2_subdev *sd, u16 reg, u8 *values, u32 n)
{
	struct lt6911uxc *lt6911uxc = to_state(sd);
	struct i2c_client *client = lt6911uxc->i2c_client;
	struct i2c_msg msgs[3];
	int err;
	u8 bank = reg >> 8;
	u8 reg_addr = reg & 0xFF;
	u8 buf[2] = {0xFF, bank};

	/* write bank */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = buf;

	/* write reg addr */
	msgs[1].addr = client->addr;
	msgs[1].flags = 0;
	msgs[1].len = 1;
	msgs[1].buf = &reg_addr;

	/* read data */
	msgs[2].addr = client->addr;
	msgs[2].flags = I2C_M_RD;
	msgs[2].len = n;
	msgs[2].buf = values;

	err = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (err != ARRAY_SIZE(msgs)) {
		v4l2_err(sd, "%s: reading register 0x%x from 0x%x failed\n",
				__func__, reg, client->addr);
		return -EIO;
	}

	return 0;
}

static int i2c_wr(struct v4l2_subdev *sd, u16 reg, u8 *values, u32 n)
{
	struct lt6911uxc *lt6911uxc = to_state(sd);
	struct i2c_client *client = lt6911uxc->i2c_client;
	struct i2c_msg msgs[2];
	int err, i;
	u8 data[I2C_MAX_XFER_SIZE];
	u8 bank = reg >> 8;
	u8 reg_addr = reg & 0xFF;
	u8 buf[2] = {0xFF, bank};

	if ((1 + n) > I2C_MAX_XFER_SIZE) {
		n = I2C_MAX_XFER_SIZE - 1;
		v4l2_warn(sd, "i2c wr reg=%04x: len=%d is too big!\n", reg,
				1 + n);
	}

	data[0] = reg_addr;
	for (i = 0; i < n; i++)
		data[i + 1] = values[i];

	/* write bank */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = buf;

	/* write reg data */
	msgs[1].addr = client->addr;
	msgs[1].flags = 0;
	msgs[1].len = 1 + n;
	msgs[1].buf = data;

	err = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (err < 0) {
		v4l2_err(sd, "%s: writing register 0x%x from 0x%x failed\n",
				__func__, reg, client->addr);
		return -EIO;
	}

	return 0;
}

static int i2c_rd8(struct v4l2_subdev *sd, u16 reg, u8 *val_p)
{
	return i2c_rd(sd, reg, val_p, 1);
}

static int i2c_wr8(struct v4l2_subdev *sd, u16 reg, u8 val)
{
	return i2c_wr(sd, reg, &val, 1);
}

static void lt6911uxc_i2c_enable(struct v4l2_subdev *sd)
{
	i2c_wr8(sd, I2C_EN_REG, I2C_ENABLE);
}

static void lt6911uxc_i2c_disable(struct v4l2_subdev *sd)
{
	i2c_wr8(sd, I2C_EN_REG, I2C_DISABLE);
}

static inline bool tx_5v_power_present(struct v4l2_subdev *sd)
{
	bool ret;
	int val, i, cnt;
	struct lt6911uxc *lt6911uxc = to_state(sd);

	/* if not use plugin det gpio */
	if (!lt6911uxc->plugin_det_gpio)
		return true;

	cnt = 0;
	for (i = 0; i < 5; i++) {
		val = gpiod_get_value(lt6911uxc->plugin_det_gpio);

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
	struct lt6911uxc *lt6911uxc = to_state(sd);

	v4l2_dbg(1, debug, sd, "%s no signal:%d\n", __func__,
			lt6911uxc->nosignal);

	return lt6911uxc->nosignal;
}

static inline bool audio_present(struct v4l2_subdev *sd)
{
	struct lt6911uxc *lt6911uxc = to_state(sd);

	return lt6911uxc->is_audio_present;
}

static int get_audio_sampling_rate(struct v4l2_subdev *sd)
{
	struct lt6911uxc *lt6911uxc = to_state(sd);

	if (no_signal(sd))
		return 0;

	return lt6911uxc->audio_sampling_rate;
}

static inline unsigned int fps_calc(const struct v4l2_bt_timings *t)
{
	if (!V4L2_DV_BT_FRAME_HEIGHT(t) || !V4L2_DV_BT_FRAME_WIDTH(t))
		return 0;

	return DIV_ROUND_CLOSEST((unsigned int)t->pixelclock,
			V4L2_DV_BT_FRAME_HEIGHT(t) * V4L2_DV_BT_FRAME_WIDTH(t));
}

static bool lt6911uxc_rcv_supported_res(struct v4l2_subdev *sd, u32 width,
		u32 height)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		if ((supported_modes[i].width == width) &&
		    (supported_modes[i].height == height)) {
			break;
		}
	}

	if (i == ARRAY_SIZE(supported_modes)) {
		v4l2_err(sd, "%s do not support res wxh: %dx%d\n", __func__,
				width, height);
		return false;
	} else {
		return true;
	}
}

static int lt6911uxc_get_detected_timings(struct v4l2_subdev *sd,
		struct v4l2_dv_timings *timings)
{
	struct lt6911uxc *lt6911uxc = to_state(sd);
	struct v4l2_bt_timings *bt = &timings->bt;
	u32 hact, vact, htotal, vtotal;
	u32 hbp, hs, hfp, vbp, vs, vfp;
	u32 pixel_clock, fps;
	u8 clk_h, clk_m, clk_l;
	u8 value, val_h, val_l;
	u32 fw_ver, mipi_byte_clk, mipi_bitrate;
	u8 fw_a, fw_b, fw_c, fw_d, lanes;
	int ret;

	memset(timings, 0, sizeof(struct v4l2_dv_timings));
	lt6911uxc_i2c_enable(sd);

	ret  = i2c_rd8(sd, FW_VER_A, &fw_a);
	ret |= i2c_rd8(sd, FW_VER_B, &fw_b);
	ret |= i2c_rd8(sd, FW_VER_C, &fw_c);
	ret |= i2c_rd8(sd, FW_VER_D, &fw_d);
	if (ret) {
		v4l2_err(sd, "%s: I2C transform err!\n", __func__);
		return -ENOLINK;
	}
	fw_ver = (fw_a << 24) | (fw_b << 16) | (fw_c <<  8) | fw_d;
	v4l2_info(sd, "read fw_version:%#x", fw_ver);
	i2c_wr8(sd, INT_COMPARE_REG, RECEIVED_INT);

	i2c_rd8(sd, INT_STATUS_86A3, &val_h);
	i2c_rd8(sd, INT_STATUS_86A5, &val_l);
	v4l2_info(sd, "int status REG_86A3:%#x, REG_86A5:%#x\n", val_h, val_l);

	i2c_rd8(sd, HDMI_VERSION, &value);
	i2c_rd8(sd, TMDS_CLK_H, &clk_h);
	i2c_rd8(sd, TMDS_CLK_M, &clk_m);
	i2c_rd8(sd, TMDS_CLK_L, &clk_l);
	pixel_clock = (((clk_h & 0xf) << 16) | (clk_m << 8) | clk_l) * 1000;
	if (value & BIT(0)) /* HDMI 2.0 */
		pixel_clock *= 4;

	i2c_rd8(sd, MIPI_LANES, &lanes);
	lt6911uxc->csi_lanes_in_use = lanes;
	i2c_wr8(sd, FM1_DET_CLK_SRC_SEL, AD_LMTX_WRITE_CLK);
	i2c_rd8(sd, FREQ_METER_H, &clk_h);
	i2c_rd8(sd, FREQ_METER_M, &clk_m);
	i2c_rd8(sd, FREQ_METER_L, &clk_l);
	mipi_byte_clk = (((clk_h & 0xf) << 16) | (clk_m << 8) | clk_l);
	mipi_bitrate = mipi_byte_clk * 8 / 1000;
	v4l2_info(sd, "MIPI Byte clk: %dKHz, MIPI bitrate: %dMbps, lanes:%d\n",
			mipi_byte_clk, mipi_bitrate, lanes);

	i2c_rd8(sd, HTOTAL_H, &val_h);
	i2c_rd8(sd, HTOTAL_L, &val_l);
	htotal = ((val_h << 8) | val_l) * 2;
	i2c_rd8(sd, VTOTAL_H, &val_h);
	i2c_rd8(sd, VTOTAL_L, &val_l);
	vtotal = (val_h << 8) | val_l;
	i2c_rd8(sd, HACT_H, &val_h);
	i2c_rd8(sd, HACT_L, &val_l);
	hact = ((val_h << 8) | val_l) * 2;
	i2c_rd8(sd, VACT_H, &val_h);
	i2c_rd8(sd, VACT_L, &val_l);
	vact = (val_h << 8) | val_l;
	i2c_rd8(sd, HS_H, &val_h);
	i2c_rd8(sd, HS_L, &val_l);
	hs = ((val_h << 8) | val_l) * 2;
	i2c_rd8(sd, VS, &value);
	vs = value;
	i2c_rd8(sd, HFP_H, &val_h);
	i2c_rd8(sd, HFP_L, &val_l);
	hfp = ((val_h << 8) | val_l) * 2;
	i2c_rd8(sd, VFP, &value);
	vfp = value;
	i2c_rd8(sd, HBP_H, &val_h);
	i2c_rd8(sd, HBP_L, &val_l);
	hbp = ((val_h << 8) | val_l) * 2;
	i2c_rd8(sd, VBP, &value);
	vbp = value;
	lt6911uxc_i2c_disable(sd);

	if (!lt6911uxc_rcv_supported_res(sd, hact, vact)) {
		lt6911uxc->nosignal = true;
		v4l2_err(sd, "%s: rcv err res, return no signal!\n", __func__);
		return -EINVAL;
	}

	lt6911uxc->nosignal = false;
	i2c_rd8(sd, AUDIO_IN_STATUS, &value);
	lt6911uxc->is_audio_present = (value & BIT(5)) ? true : false;
	i2c_rd8(sd, AUDIO_SAMPLE_RATAE_H, &val_h);
	i2c_rd8(sd, AUDIO_SAMPLE_RATAE_L, &val_l);
	lt6911uxc->audio_sampling_rate = ((val_h << 8) | val_l) + 2;
	v4l2_info(sd, "is_audio_present: %d, audio_sampling_rate: %dKhz\n",
			lt6911uxc->is_audio_present,
			lt6911uxc->audio_sampling_rate);

	timings->type = V4L2_DV_BT_656_1120;
	bt->width = hact;
	bt->height = vact;
	bt->vsync = vs;
	bt->hsync = hs;
	bt->pixelclock = pixel_clock;
	bt->hfrontporch = hfp;
	bt->vfrontporch = vfp;
	bt->hbackporch = hbp;
	bt->vbackporch = vbp;
	fps = fps_calc(bt);

	/* for interlaced res 1080i 576i 480i*/
	if ((hact == 1920 && vact == 540) || (hact == 1440 && vact == 288)
			|| (hact == 1440 && vact == 240)) {
		bt->interlaced = V4L2_DV_INTERLACED;
		bt->height *= 2;
		bt->il_vsync = bt->vsync + 1;
	} else {
		bt->interlaced = V4L2_DV_PROGRESSIVE;
	}

	v4l2_info(sd, "act:%dx%d, total:%dx%d, pixclk:%d, fps:%d\n",
			hact, vact, htotal, vtotal, pixel_clock, fps);
	v4l2_info(sd, "hfp:%d, hs:%d, hbp:%d, vfp:%d, vs:%d, vbp:%d, inerlaced:%d\n",
			bt->hfrontporch, bt->hsync, bt->hbackporch, bt->vfrontporch,
			bt->vsync, bt->vbackporch, bt->interlaced);

	return 0;
}

static void lt6911uxc_config_hpd(struct v4l2_subdev *sd)
{
	struct lt6911uxc *lt6911uxc = to_state(sd);
	bool plugin;

	plugin = tx_5v_power_present(sd);
	v4l2_dbg(2, debug, sd, "%s: plugin: %d\n", __func__, plugin);

	if (plugin) {
		gpiod_set_value(lt6911uxc->hpd_ctl_gpio, 1);
	} else {
		lt6911uxc->nosignal = true;
		gpiod_set_value(lt6911uxc->hpd_ctl_gpio, 0);
	}
}

static void lt6911uxc_delayed_work_enable_hotplug(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct lt6911uxc *lt6911uxc = container_of(dwork,
			struct lt6911uxc, delayed_work_enable_hotplug);
	struct v4l2_subdev *sd = &lt6911uxc->sd;

	v4l2_dbg(2, debug, sd, "%s:\n", __func__);

	v4l2_ctrl_s_ctrl(lt6911uxc->detect_tx_5v_ctrl, tx_5v_power_present(sd));
	lt6911uxc_config_hpd(sd);
}

static void lt6911uxc_delayed_work_res_change(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct lt6911uxc *lt6911uxc = container_of(dwork,
			struct lt6911uxc, delayed_work_res_change);
	struct v4l2_subdev *sd = &lt6911uxc->sd;

	v4l2_dbg(2, debug, sd, "%s:\n", __func__);
	lt6911uxc_format_change(sd);
}

static int lt6911uxc_s_ctrl_detect_tx_5v(struct v4l2_subdev *sd)
{
	struct lt6911uxc *lt6911uxc = to_state(sd);

	return v4l2_ctrl_s_ctrl(lt6911uxc->detect_tx_5v_ctrl,
			tx_5v_power_present(sd));
}

static int lt6911uxc_s_ctrl_audio_sampling_rate(struct v4l2_subdev *sd)
{
	struct lt6911uxc *lt6911uxc = to_state(sd);

	return v4l2_ctrl_s_ctrl(lt6911uxc->audio_sampling_rate_ctrl,
			get_audio_sampling_rate(sd));
}

static int lt6911uxc_s_ctrl_audio_present(struct v4l2_subdev *sd)
{
	struct lt6911uxc *lt6911uxc = to_state(sd);

	return v4l2_ctrl_s_ctrl(lt6911uxc->audio_present_ctrl,
			audio_present(sd));
}

static int lt6911uxc_update_controls(struct v4l2_subdev *sd)
{
	int ret = 0;

	ret |= lt6911uxc_s_ctrl_detect_tx_5v(sd);
	ret |= lt6911uxc_s_ctrl_audio_sampling_rate(sd);
	ret |= lt6911uxc_s_ctrl_audio_present(sd);

	return ret;
}

static inline void enable_stream(struct v4l2_subdev *sd, bool enable)
{
	v4l2_dbg(2, debug, sd, "%s: %sable\n",
			__func__, enable ? "en" : "dis");
}

static void lt6911uxc_format_change(struct v4l2_subdev *sd)
{
	struct lt6911uxc *lt6911uxc = to_state(sd);
	struct v4l2_dv_timings timings;
	const struct v4l2_event lt6911uxc_ev_fmt = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
	};

	if (lt6911uxc_get_detected_timings(sd, &timings)) {
		enable_stream(sd, false);

		v4l2_dbg(1, debug, sd, "%s: No signal\n", __func__);
	} else {
		if (!v4l2_match_dv_timings(&lt6911uxc->timings, &timings, 0,
					false)) {
			enable_stream(sd, false);
			/* automatically set timing rather than set by user */
			lt6911uxc_s_dv_timings(sd, &timings);
			v4l2_print_dv_timings(sd->name,
					"Format_change: New format: ",
					&timings, false);
		}
	}

	if (sd->devnode)
		v4l2_subdev_notify_event(sd, &lt6911uxc_ev_fmt);
}

static int lt6911uxc_isr(struct v4l2_subdev *sd, u32 status, bool *handled)
{
	struct lt6911uxc *lt6911uxc = to_state(sd);

	schedule_delayed_work(&lt6911uxc->delayed_work_res_change, HZ / 20);
	*handled = true;

	return 0;
}

static irqreturn_t lt6911uxc_res_change_irq_handler(int irq, void *dev_id)
{
	struct lt6911uxc *lt6911uxc = dev_id;
	bool handled;

	lt6911uxc_isr(&lt6911uxc->sd, 0, &handled);

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

static irqreturn_t plugin_detect_irq_handler(int irq, void *dev_id)
{
	struct lt6911uxc *lt6911uxc = dev_id;
	struct v4l2_subdev *sd = &lt6911uxc->sd;

	/* control hpd output level after 25ms */
	schedule_delayed_work(&lt6911uxc->delayed_work_enable_hotplug,
			HZ / 40);
	tx_5v_power_present(sd);

	return IRQ_HANDLED;
}

static int lt6911uxc_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
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

static int lt6911uxc_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	*status = 0;
	*status |= no_signal(sd) ? V4L2_IN_ST_NO_SIGNAL : 0;
	v4l2_dbg(1, debug, sd, "%s: status = 0x%x\n", __func__, *status);

	return 0;
}

static int lt6911uxc_s_dv_timings(struct v4l2_subdev *sd,
				 struct v4l2_dv_timings *timings)
{
	struct lt6911uxc *lt6911uxc = to_state(sd);

	if (!timings)
		return -EINVAL;

	if (debug)
		v4l2_print_dv_timings(sd->name, "s_dv_timings: ", timings, false);

	if (v4l2_match_dv_timings(&lt6911uxc->timings, timings, 0, false)) {
		v4l2_dbg(1, debug, sd, "%s: no change\n", __func__);
		return 0;
	}

	if (!v4l2_valid_dv_timings(timings,
				&lt6911uxc_timings_cap, NULL, NULL)) {
		v4l2_dbg(1, debug, sd, "%s: timings out of range\n", __func__);
		return -ERANGE;
	}

	lt6911uxc->timings = *timings;

	enable_stream(sd, false);

	return 0;
}

static int lt6911uxc_g_dv_timings(struct v4l2_subdev *sd,
				 struct v4l2_dv_timings *timings)
{
	struct lt6911uxc *lt6911uxc = to_state(sd);

	*timings = lt6911uxc->timings;

	return 0;
}

static int lt6911uxc_enum_dv_timings(struct v4l2_subdev *sd,
				    struct v4l2_enum_dv_timings *timings)
{
	if (timings->pad != 0)
		return -EINVAL;

	return v4l2_enum_dv_timings_cap(timings,
			&lt6911uxc_timings_cap, NULL, NULL);
}

static int lt6911uxc_query_dv_timings(struct v4l2_subdev *sd,
		struct v4l2_dv_timings *timings)
{
	struct lt6911uxc *lt6911uxc = to_state(sd);

	*timings = lt6911uxc->timings;
	if (debug)
		v4l2_print_dv_timings(sd->name, "query_dv_timings: ", timings, false);

	if (!v4l2_valid_dv_timings(timings, &lt6911uxc_timings_cap, NULL,
				NULL)) {
		v4l2_dbg(1, debug, sd, "%s: timings out of range\n", __func__);

		return -ERANGE;
	}

	return 0;
}

static int lt6911uxc_dv_timings_cap(struct v4l2_subdev *sd,
		struct v4l2_dv_timings_cap *cap)
{
	if (cap->pad != 0)
		return -EINVAL;

	*cap = lt6911uxc_timings_cap;

	return 0;
}

static int lt6911uxc_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
			     struct v4l2_mbus_config *cfg)
{
	struct lt6911uxc *lt6911uxc = to_state(sd);

	cfg->type = V4L2_MBUS_CSI2_DPHY;
	cfg->flags = V4L2_MBUS_CSI2_CONTINUOUS_CLOCK | V4L2_MBUS_CSI2_CHANNEL_0;

	switch (lt6911uxc->csi_lanes_in_use) {
	case 1:
		cfg->flags |= V4L2_MBUS_CSI2_1_LANE;
		break;
	case 2:
		cfg->flags |= V4L2_MBUS_CSI2_2_LANE;
		break;
	case 3:
		cfg->flags |= V4L2_MBUS_CSI2_3_LANE;
		break;
	case 4:
		cfg->flags |= V4L2_MBUS_CSI2_4_LANE;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int lt6911uxc_s_stream(struct v4l2_subdev *sd, int enable)
{
	enable_stream(sd, enable);

	return 0;
}

static int lt6911uxc_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	switch (code->index) {
	case 0:
		code->code = LT6911UXC_MEDIA_BUS_FMT;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int lt6911uxc_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != LT6911UXC_MEDIA_BUS_FMT)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int lt6911uxc_enum_frame_interval(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fie->code != LT6911UXC_MEDIA_BUS_FMT)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;

	return 0;
}

static int lt6911uxc_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct lt6911uxc *lt6911uxc = to_state(sd);

	mutex_lock(&lt6911uxc->confctl_mutex);
	format->format.code = lt6911uxc->mbus_fmt_code;
	format->format.width = lt6911uxc->timings.bt.width;
	format->format.height = lt6911uxc->timings.bt.height;
	format->format.field =
		lt6911uxc->timings.bt.interlaced ?
		V4L2_FIELD_INTERLACED : V4L2_FIELD_NONE;
	format->format.colorspace = V4L2_COLORSPACE_SRGB;
	mutex_unlock(&lt6911uxc->confctl_mutex);

	v4l2_dbg(1, debug, sd, "%s: fmt code:%d, w:%d, h:%d, field mode:%s\n",
		__func__, format->format.code, format->format.width, format->format.height,
		(format->format.field == V4L2_FIELD_INTERLACED) ? "I" : "P");

	return 0;
}

static int lt6911uxc_get_reso_dist(const struct lt6911uxc_mode *mode,
		struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct lt6911uxc_mode *
lt6911uxc_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = lt6911uxc_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int lt6911uxc_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct lt6911uxc *lt6911uxc = to_state(sd);
	const struct lt6911uxc_mode *mode;
	int index;

	/* is overwritten by get_fmt */
	u32 code = format->format.code;
	int ret = lt6911uxc_get_fmt(sd, cfg, format);

	format->format.code = code;

	if (ret)
		return ret;

	switch (code) {
	case LT6911UXC_MEDIA_BUS_FMT:
		break;

	default:
		return -EINVAL;
	}

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	lt6911uxc->mbus_fmt_code = format->format.code;
	mode = lt6911uxc_find_best_fit(format);
	lt6911uxc->cur_mode = mode;
	enable_stream(sd, false);

	if (((mode->width == 720) && (mode->height == 576)) ||
	    ((mode->width == 720) && (mode->height == 480)))
		index = 1;
	else
		index = 0;

	__v4l2_ctrl_s_ctrl(lt6911uxc->link_freq, index);
	v4l2_dbg(1, debug, sd, "%s res wxh:%dx%d, link freq:%llu", __func__,
			mode->width, mode->height, link_freq_menu_items[index]);

	return 0;
}

static int lt6911uxc_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct lt6911uxc *lt6911uxc = to_state(sd);
	const struct lt6911uxc_mode *mode = lt6911uxc->cur_mode;

	mutex_lock(&lt6911uxc->confctl_mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&lt6911uxc->confctl_mutex);

	return 0;
}

static void lt6911uxc_get_module_inf(struct lt6911uxc *lt6911uxc,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, LT6911UXC_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, lt6911uxc->module_name, sizeof(inf->base.module));
	strscpy(inf->base.lens, lt6911uxc->len_name, sizeof(inf->base.lens));
}

static long lt6911uxc_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct lt6911uxc *lt6911uxc = to_state(sd);
	long ret = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		lt6911uxc_get_module_inf(lt6911uxc, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDMI_MODE:
		*(int *)arg = RKMODULE_HDMIIN_MODE;
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long lt6911uxc_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	long ret;
	int *seq;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = lt6911uxc_ioctl(sd, cmd, inf);
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

		ret = lt6911uxc_ioctl(sd, cmd, seq);
		if (!ret) {
			ret = copy_to_user(up, seq, sizeof(*seq));
			if (ret)
				ret = -EFAULT;
		}
		kfree(seq);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static const struct v4l2_subdev_core_ops lt6911uxc_core_ops = {
	.interrupt_service_routine = lt6911uxc_isr,
	.subscribe_event = lt6911uxc_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
	.ioctl = lt6911uxc_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = lt6911uxc_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops lt6911uxc_video_ops = {
	.g_input_status = lt6911uxc_g_input_status,
	.s_dv_timings = lt6911uxc_s_dv_timings,
	.g_dv_timings = lt6911uxc_g_dv_timings,
	.query_dv_timings = lt6911uxc_query_dv_timings,
	.s_stream = lt6911uxc_s_stream,
	.g_frame_interval = lt6911uxc_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops lt6911uxc_pad_ops = {
	.enum_mbus_code = lt6911uxc_enum_mbus_code,
	.enum_frame_size = lt6911uxc_enum_frame_sizes,
	.enum_frame_interval = lt6911uxc_enum_frame_interval,
	.set_fmt = lt6911uxc_set_fmt,
	.get_fmt = lt6911uxc_get_fmt,
	.enum_dv_timings = lt6911uxc_enum_dv_timings,
	.dv_timings_cap = lt6911uxc_dv_timings_cap,
	.get_mbus_config = lt6911uxc_g_mbus_config,
};

static const struct v4l2_subdev_ops lt6911uxc_ops = {
	.core = &lt6911uxc_core_ops,
	.video = &lt6911uxc_video_ops,
	.pad = &lt6911uxc_pad_ops,
};

static const struct v4l2_ctrl_config lt6911uxc_ctrl_audio_sampling_rate = {
	.id = RK_V4L2_CID_AUDIO_SAMPLING_RATE,
	.name = "Audio sampling rate",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 768000,
	.step = 1,
	.def = 0,
	.flags = V4L2_CTRL_FLAG_READ_ONLY,
};

static const struct v4l2_ctrl_config lt6911uxc_ctrl_audio_present = {
	.id = RK_V4L2_CID_AUDIO_PRESENT,
	.name = "Audio present",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 0,
	.flags = V4L2_CTRL_FLAG_READ_ONLY,
};

static void lt6911uxc_reset(struct lt6911uxc *lt6911uxc)
{
	gpiod_set_value(lt6911uxc->reset_gpio, 0);
	usleep_range(2000, 2100);
	gpiod_set_value(lt6911uxc->reset_gpio, 1);
	usleep_range(120*1000, 121*1000);
	gpiod_set_value(lt6911uxc->reset_gpio, 0);
	usleep_range(300*1000, 310*1000);
}

static int lt6911uxc_init_v4l2_ctrls(struct lt6911uxc *lt6911uxc)
{
	struct v4l2_subdev *sd;
	int ret;

	sd = &lt6911uxc->sd;
	ret = v4l2_ctrl_handler_init(&lt6911uxc->hdl, 5);
	if (ret)
		return ret;

	lt6911uxc->link_freq = v4l2_ctrl_new_int_menu(&lt6911uxc->hdl, NULL,
			V4L2_CID_LINK_FREQ,
			ARRAY_SIZE(link_freq_menu_items) - 1, 0,
			link_freq_menu_items);
	v4l2_ctrl_new_std(&lt6911uxc->hdl, NULL, V4L2_CID_PIXEL_RATE,
			  0, LT6911UXC_PIXEL_RATE, 1, LT6911UXC_PIXEL_RATE);

	lt6911uxc->detect_tx_5v_ctrl = v4l2_ctrl_new_std(&lt6911uxc->hdl,
			NULL, V4L2_CID_DV_RX_POWER_PRESENT,
			0, 1, 0, 0);

	lt6911uxc->audio_sampling_rate_ctrl =
		v4l2_ctrl_new_custom(&lt6911uxc->hdl,
				&lt6911uxc_ctrl_audio_sampling_rate, NULL);
	lt6911uxc->audio_present_ctrl = v4l2_ctrl_new_custom(&lt6911uxc->hdl,
			&lt6911uxc_ctrl_audio_present, NULL);

	sd->ctrl_handler = &lt6911uxc->hdl;
	if (lt6911uxc->hdl.error) {
		ret = lt6911uxc->hdl.error;
		v4l2_err(sd, "cfg v4l2 ctrls failed! ret:%d\n", ret);
		return ret;
	}

	if (lt6911uxc_update_controls(sd)) {
		ret = -ENODEV;
		v4l2_err(sd, "update v4l2 ctrls failed! ret:%d\n", ret);
		return ret;
	}

	return 0;
}

static int lt6911uxc_check_chip_id(struct lt6911uxc *lt6911uxc)
{
	struct device *dev = &lt6911uxc->i2c_client->dev;
	struct v4l2_subdev *sd = &lt6911uxc->sd;
	u8 fw_a, fw_b, fw_c, fw_d;
	u8 id_h, id_l;
	u32 chipid, fw_ver;
	int ret;

	lt6911uxc_i2c_enable(sd);
	ret  = i2c_rd8(sd, CHIPID_L, &id_l);
	ret |= i2c_rd8(sd, CHIPID_H, &id_h);

	ret |= i2c_rd8(sd, FW_VER_A, &fw_a);
	ret |= i2c_rd8(sd, FW_VER_B, &fw_b);
	ret |= i2c_rd8(sd, FW_VER_C, &fw_c);
	ret |= i2c_rd8(sd, FW_VER_D, &fw_d);
	lt6911uxc_i2c_disable(sd);

	if (!ret) {
		chipid = (id_h << 8) | id_l;
		if (chipid != LT6911UXC_CHIPID) {
			dev_err(dev, "chipid err, read:%#x, expect:%#x\n",
					chipid, LT6911UXC_CHIPID);
			return -EINVAL;
		}

		fw_ver = (fw_a << 24) | (fw_b << 16) | (fw_c <<  8) | fw_d;
		dev_info(dev, "chipid ok, id:%#x, fw_ver:%#x", chipid, fw_ver);
		ret = 0;
	} else {
		dev_err(dev, "%s i2c trans failed!\n", __func__);
	}

	return ret;
}

#ifdef CONFIG_OF
static int lt6911uxc_parse_of(struct lt6911uxc *lt6911uxc)
{
	struct device *dev = &lt6911uxc->i2c_client->dev;
	struct device_node *node = dev->of_node;
	struct v4l2_fwnode_endpoint endpoint = { .bus_type = 0 };
	struct device_node *ep;
	int ret;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
			&lt6911uxc->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
			&lt6911uxc->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
			&lt6911uxc->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
			&lt6911uxc->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	lt6911uxc->power_gpio = devm_gpiod_get_optional(dev, "power",
			GPIOD_OUT_LOW);
	if (IS_ERR(lt6911uxc->power_gpio)) {
		dev_err(dev, "failed to get power gpio\n");
		ret = PTR_ERR(lt6911uxc->power_gpio);
		return ret;
	}

	lt6911uxc->reset_gpio = devm_gpiod_get_optional(dev, "reset",
			GPIOD_OUT_LOW);
	if (IS_ERR(lt6911uxc->reset_gpio)) {
		dev_err(dev, "failed to get reset gpio\n");
		ret = PTR_ERR(lt6911uxc->reset_gpio);
		return ret;
	}

	lt6911uxc->plugin_det_gpio = devm_gpiod_get_optional(dev, "plugin-det",
			GPIOD_IN);
	if (IS_ERR(lt6911uxc->plugin_det_gpio)) {
		dev_err(dev, "failed to get plugin det gpio\n");
		ret = PTR_ERR(lt6911uxc->plugin_det_gpio);
		return ret;
	}

	lt6911uxc->hpd_ctl_gpio = devm_gpiod_get_optional(dev, "hpd-ctl",
			GPIOD_OUT_HIGH);
	if (IS_ERR(lt6911uxc->hpd_ctl_gpio)) {
		dev_err(dev, "failed to get hpd ctl gpio\n");
		ret = PTR_ERR(lt6911uxc->hpd_ctl_gpio);
		return ret;
	}

	ep = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!ep) {
		dev_err(dev, "missing endpoint node\n");
		ret = -EINVAL;
		return ret;
	}

	ret = v4l2_fwnode_endpoint_alloc_parse(of_fwnode_handle(ep), &endpoint);
	if (ret) {
		dev_err(dev, "failed to parse endpoint\n");
		goto put_node;
	}

	if (endpoint.bus_type != V4L2_MBUS_CSI2_DPHY ||
			endpoint.bus.mipi_csi2.num_data_lanes == 0) {
		dev_err(dev, "missing CSI-2 properties in endpoint\n");
		ret = -EINVAL;
		goto free_endpoint;
	}

	lt6911uxc->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(lt6911uxc->xvclk)) {
		dev_err(dev, "failed to get xvclk\n");
		ret = -EINVAL;
		goto free_endpoint;
	}

	ret = clk_prepare_enable(lt6911uxc->xvclk);
	if (ret) {
		dev_err(dev, "Failed! to enable xvclk\n");
		goto free_endpoint;
	}

	lt6911uxc->csi_lanes_in_use = endpoint.bus.mipi_csi2.num_data_lanes;
	lt6911uxc->bus = endpoint.bus.mipi_csi2;
	lt6911uxc->enable_hdcp = false;

	gpiod_set_value(lt6911uxc->hpd_ctl_gpio, 0);
	gpiod_set_value(lt6911uxc->power_gpio, 1);
	lt6911uxc_reset(lt6911uxc);

	ret = 0;

free_endpoint:
	v4l2_fwnode_endpoint_free(&endpoint);
put_node:
	of_node_put(ep);
	return ret;
}
#else
static inline int lt6911uxc_parse_of(struct lt6911uxc *lt6911uxc)
{
	return -ENODEV;
}
#endif

static int lt6911uxc_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct lt6911uxc *lt6911uxc;
	struct v4l2_subdev *sd;
	struct device *dev = &client->dev;
	char facing[2];
	int err;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	lt6911uxc = devm_kzalloc(dev, sizeof(struct lt6911uxc), GFP_KERNEL);
	if (!lt6911uxc)
		return -ENOMEM;

	sd = &lt6911uxc->sd;
	lt6911uxc->i2c_client = client;
	lt6911uxc->cur_mode = &supported_modes[0];
	lt6911uxc->mbus_fmt_code = LT6911UXC_MEDIA_BUS_FMT;

	err = lt6911uxc_parse_of(lt6911uxc);
	if (err) {
		v4l2_err(sd, "lt6911uxc_parse_of failed! err:%d\n", err);
		return err;
	}

	err = lt6911uxc_check_chip_id(lt6911uxc);
	if (err < 0)
		return err;

	/* after the CPU actively accesses the lt6911uxc through I2C,
	 * a reset operation is required.
	 */
	lt6911uxc_reset(lt6911uxc);

	mutex_init(&lt6911uxc->confctl_mutex);
	err = lt6911uxc_init_v4l2_ctrls(lt6911uxc);
	if (err)
		goto err_free_hdl;

	client->flags |= I2C_CLIENT_SCCB;
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	v4l2_i2c_subdev_init(sd, client, &lt6911uxc_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
#endif

#if defined(CONFIG_MEDIA_CONTROLLER)
	lt6911uxc->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	err = media_entity_pads_init(&sd->entity, 1, &lt6911uxc->pad);
	if (err < 0) {
		v4l2_err(sd, "media entity init failed! err: %d\n", err);
		goto err_free_hdl;
	}
#endif
	memset(facing, 0, sizeof(facing));
	if (strcmp(lt6911uxc->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 lt6911uxc->module_index, facing,
		 LT6911UXC_NAME, dev_name(sd->dev));
	err = v4l2_async_register_subdev_sensor_common(sd);
	if (err < 0) {
		v4l2_err(sd, "v4l2 register subdev failed! err:%d\n", err);
		goto err_clean_entity;
	}

	INIT_DELAYED_WORK(&lt6911uxc->delayed_work_enable_hotplug,
			lt6911uxc_delayed_work_enable_hotplug);
	INIT_DELAYED_WORK(&lt6911uxc->delayed_work_res_change,
			lt6911uxc_delayed_work_res_change);

	if (lt6911uxc->i2c_client->irq) {
		v4l2_dbg(1, debug, sd, "cfg lt6911uxc irq!\n");
		err = devm_request_threaded_irq(dev,
				lt6911uxc->i2c_client->irq,
				NULL, lt6911uxc_res_change_irq_handler,
				IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				"lt6911uxc", lt6911uxc);
		if (err) {
			v4l2_err(sd, "request irq failed! err:%d\n", err);
			goto err_work_queues;
		}
	} else {
		err = -EINVAL;
		v4l2_err(sd, "no irq cfg failed!\n");
		goto err_work_queues;
	}

	lt6911uxc->plugin_irq = gpiod_to_irq(lt6911uxc->plugin_det_gpio);
	if (lt6911uxc->plugin_irq < 0)
		dev_err(dev, "failed to get plugin det irq, maybe no use\n");

	err = devm_request_threaded_irq(dev, lt6911uxc->plugin_irq, NULL,
			plugin_detect_irq_handler, IRQF_TRIGGER_FALLING |
			IRQF_TRIGGER_RISING | IRQF_ONESHOT, "lt6911uxc",
			lt6911uxc);
	if (err)
		dev_err(dev, "failed to register plugin det irq (%d), maybe no use\n", err);

	err = v4l2_ctrl_handler_setup(sd->ctrl_handler);
	if (err) {
		v4l2_err(sd, "v4l2 ctrl handler setup failed! err:%d\n", err);
		goto err_work_queues;
	}

	lt6911uxc_config_hpd(sd);
	v4l2_info(sd, "%s found @ 0x%x (%s)\n", client->name,
			client->addr << 1, client->adapter->name);

	return 0;

err_work_queues:
	cancel_delayed_work(&lt6911uxc->delayed_work_enable_hotplug);
	cancel_delayed_work(&lt6911uxc->delayed_work_res_change);
err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_free_hdl:
	v4l2_ctrl_handler_free(&lt6911uxc->hdl);
	mutex_destroy(&lt6911uxc->confctl_mutex);
	return err;
}

static int lt6911uxc_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct lt6911uxc *lt6911uxc = to_state(sd);

	cancel_delayed_work_sync(&lt6911uxc->delayed_work_enable_hotplug);
	cancel_delayed_work_sync(&lt6911uxc->delayed_work_res_change);
	v4l2_async_unregister_subdev(sd);
	v4l2_device_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&lt6911uxc->hdl);
	mutex_destroy(&lt6911uxc->confctl_mutex);
	clk_disable_unprepare(lt6911uxc->xvclk);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id lt6911uxc_of_match[] = {
	{ .compatible = "lontium,lt6911uxc" },
	{},
};
MODULE_DEVICE_TABLE(of, lt6911uxc_of_match);
#endif

static struct i2c_driver lt6911uxc_driver = {
	.driver = {
		.name = LT6911UXC_NAME,
		.of_match_table = of_match_ptr(lt6911uxc_of_match),
	},
	.probe = lt6911uxc_probe,
	.remove = lt6911uxc_remove,
};

static int __init lt6911uxc_driver_init(void)
{
	return i2c_add_driver(&lt6911uxc_driver);
}

static void __exit lt6911uxc_driver_exit(void)
{
	i2c_del_driver(&lt6911uxc_driver);
}

late_initcall(lt6911uxc_driver_init);
module_exit(lt6911uxc_driver_exit);

MODULE_DESCRIPTION("Lontium LT6911UXC HDMI to MIPI CSI-2 bridge driver");
MODULE_AUTHOR("Dingxian Wen <shawn.wen@rock-chips.com>");
MODULE_LICENSE("GPL v2");

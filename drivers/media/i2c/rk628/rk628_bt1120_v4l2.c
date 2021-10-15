// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Shunqing Chen <csq@rock-chips.com>
 */

#include <linux/clk.h>
#include <linux/compat.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/rk-camera-module.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/v4l2-controls.h>
#include <linux/v4l2-dv-timings.h>
#include <linux/version.h>
#include <linux/videodev2.h>
#include <linux/workqueue.h>
#include <media/v4l2-controls_rockchip.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <video/videomode.h>

#include "rk628.h"
#include "rk628_combrxphy.h"
#include "rk628_cru.h"
#include "rk628_hdmirx.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-3)");

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x0, 0x1)
#define RK628_BT1120_NAME		"rk628-bt1120"

#define EDID_NUM_BLOCKS_MAX		2
#define EDID_BLOCK_SIZE			128

#define RK628_CSI_LINK_FREQ_LOW		350000000
#define RK628_CSI_LINK_FREQ_HIGH	400000000
#define RK628_CSI_PIXEL_RATE_LOW	400000000
#define RK628_CSI_PIXEL_RATE_HIGH	600000000
#define MIPI_DATARATE_MBPS_LOW		750
#define MIPI_DATARATE_MBPS_HIGH		1250

#define POLL_INTERVAL_MS		1000
#define MODETCLK_CNT_NUM		1000
#define MODETCLK_HZ			49500000
#define RXPHY_CFG_MAX_TIMES		15
#define CSITX_ERR_RETRY_TIMES		3

#define USE_4_LANES			4
#define YUV422_8BIT			0x1e
/* Test Code: 0x44 (HS RX Control of Lane 0) */
#define HSFREQRANGE(x)			UPDATE(x, 6, 1)

struct rk628_bt1120 {
	struct device *dev;
	struct i2c_client *i2c_client;
	struct rk628 *rk628;
	struct media_pad pad;
	struct v4l2_subdev sd;
	struct v4l2_dv_timings src_timings;
	struct v4l2_dv_timings timings;
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl *detect_tx_5v_ctrl;
	struct v4l2_ctrl *audio_sampling_rate_ctrl;
	struct v4l2_ctrl *audio_present_ctrl;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct gpio_desc *enable_gpio;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *power_gpio;
	struct gpio_desc *plugin_det_gpio;
	struct clk *soc_24M;
	struct clk *clk_hdmirx_aud;
	struct clk *clk_vop;
	struct clk *clk_rx_read;
	struct delayed_work delayed_work_enable_hotplug;
	struct delayed_work delayed_work_res_change;
	struct timer_list timer;
	struct work_struct work_i2c_poll;
	struct mutex confctl_mutex;
	const struct rk628_bt1120_mode *cur_mode;
	const char *module_facing;
	const char *module_name;
	const char *len_name;
	u32 module_index;
	u8 edid_blocks_written;
	u64 lane_mbps;
	u8 bt1120_lanes_in_use;
	u32 mbus_fmt_code;
	u8 fps;
	u32 stream_state;
	int hdmirx_irq;
	int plugin_irq;
	bool nosignal;
	bool rxphy_pwron;
	bool enable_hdcp;
	bool scaler_en;
	bool hpd_output_inverted;
	bool avi_rcv_rdy;
	bool vid_ints_en;
	bool dual_edge;
	struct rk628_hdcp hdcp;
	bool i2s_enable_default;
	HAUDINFO audio_info;
};

struct rk628_bt1120_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
};

static const s64 link_freq_menu_items[] = {
	RK628_CSI_LINK_FREQ_LOW,
	RK628_CSI_LINK_FREQ_HIGH,
};

static const struct v4l2_dv_timings_cap rk628_bt1120_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	/* keep this initialization for compatibility with GCC < 4.4.6 */
	.reserved = { 0 },
	V4L2_INIT_BT_TIMINGS(1, 10000, 1, 10000, 0, 400000000,
			V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
			V4L2_DV_BT_STD_GTF | V4L2_DV_BT_STD_CVT,
			V4L2_DV_BT_CAP_PROGRESSIVE | V4L2_DV_BT_CAP_INTERLACED |
			V4L2_DV_BT_CAP_REDUCED_BLANKING |
			V4L2_DV_BT_CAP_CUSTOM)
};

static u8 edid_init_data[] = {
	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
	0x49, 0x73, 0x8D, 0x62, 0x00, 0x88, 0x88, 0x88,
	0x08, 0x1E, 0x01, 0x03, 0x80, 0x00, 0x00, 0x78,
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
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01, 0x18,

	0x02, 0x03, 0x19, 0x71, 0x46, 0x90, 0x22, 0x04,
	0x11, 0x02, 0x01, 0x23, 0x09, 0x07, 0x01, 0x83,
	0x01, 0x00, 0x00, 0x65, 0x03, 0x0C, 0x00, 0x10,
	0x00, 0x8C, 0x0A, 0xD0, 0x8A, 0x20, 0xE0, 0x2D,
	0x10, 0x10, 0x3E, 0x96, 0x00, 0x13, 0x8E, 0x21,
	0x00, 0x00, 0x1E, 0xD8, 0x09, 0x80, 0xA0, 0x20,
	0xE0, 0x2D, 0x10, 0x10, 0x60, 0xA2, 0x00, 0xC4,
	0x8E, 0x21, 0x00, 0x00, 0x18, 0x02, 0x3A, 0x80,
	0xD0, 0x72, 0x38, 0x2D, 0x40, 0x10, 0x2C, 0x45,
	0x80, 0x20, 0xC2, 0x31, 0x00, 0x00, 0x1E, 0x01,
	0x1D, 0x80, 0x18, 0x71, 0x38, 0x2D, 0x40, 0x58,
	0x2C, 0x45, 0x00, 0xC0, 0x6C, 0x00, 0x00, 0x00,
	0x18, 0x01, 0x1D, 0x80, 0x18, 0x71, 0x1C, 0x16,
	0x20, 0x58, 0x2C, 0x25, 0x00, 0xC0, 0x6C, 0x00,
	0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x45,
};

static const struct rk628_bt1120_mode supported_modes[] = {
	{
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 2200,
		.vts_def = 1125,
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

static struct v4l2_dv_timings dst_timing = {
	.type = V4L2_DV_BT_656_1120,
	.bt = {
		.interlaced = V4L2_DV_PROGRESSIVE,
		.width = 1920,
		.height = 1080,
		.hfrontporch = 88,
		.hsync = 44,
		.hbackporch = 148,
		.vfrontporch = 4,
		.vsync = 5,
		.vbackporch = 36,
		.pixelclock = 148500000,
	},
};

static void rk628_post_process_setup(struct v4l2_subdev *sd);
static void rk628_bt1120_enable_interrupts(struct v4l2_subdev *sd, bool en);
static int rk628_bt1120_s_ctrl_detect_tx_5v(struct v4l2_subdev *sd);
static int rk628_bt1120_s_dv_timings(struct v4l2_subdev *sd,
				 struct v4l2_dv_timings *timings);
static int rk628_bt1120_s_edid(struct v4l2_subdev *sd,
				struct v4l2_subdev_edid *edid);
static int rk628_hdmirx_phy_power_on(struct v4l2_subdev *sd);
static int rk628_hdmirx_phy_power_off(struct v4l2_subdev *sd);
static int rk628_hdmirx_phy_setup(struct v4l2_subdev *sd);
static void rk628_bt1120_format_change(struct v4l2_subdev *sd);
static void enable_stream(struct v4l2_subdev *sd, bool enable);
static void rk628_hdmirx_vid_enable(struct v4l2_subdev *sd, bool en);
static void rk628_hdmirx_hpd_ctrl(struct v4l2_subdev *sd, bool en);
static void rk628_hdmirx_controller_reset(struct v4l2_subdev *sd);
static void rk628_bt1120_initial_setup(struct v4l2_subdev *sd);

static inline struct rk628_bt1120 *to_bt1120(struct v4l2_subdev *sd)
{
	return container_of(sd, struct rk628_bt1120, sd);
}

static bool tx_5v_power_present(struct v4l2_subdev *sd)
{
	bool ret;
	int val, i, cnt;
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);

	/* Direct Mode */
	if (!bt1120->plugin_det_gpio)
		return true;

	cnt = 0;
	for (i = 0; i < 5; i++) {
		val = gpiod_get_value(bt1120->plugin_det_gpio);
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
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);

	v4l2_dbg(1, debug, sd, "%s no signal:%d\n", __func__, bt1120->nosignal);
	return bt1120->nosignal;
}

static inline bool audio_present(struct v4l2_subdev *sd)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);

	return rk628_hdmirx_audio_present(bt1120->audio_info);
}

static int get_audio_sampling_rate(struct v4l2_subdev *sd)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);

	if (no_signal(sd))
		return 0;

	return rk628_hdmirx_audio_fs(bt1120->audio_info);
}

static void rk628_hdmirx_ctrl_enable(struct v4l2_subdev *sd, int en)
{
	u32 mask;
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);

	if (en) {
		/* don't enable audio until N CTS updated */
		mask = HDMI_ENABLE_MASK;
		v4l2_dbg(1, debug, sd, "%s: %#x %d\n", __func__, mask, en);
		rk628_i2c_update_bits(bt1120->rk628, HDMI_RX_DMI_DISABLE_IF,
				   mask, HDMI_ENABLE(1) | AUD_ENABLE(1));
	} else {
		mask = AUD_ENABLE_MASK | HDMI_ENABLE_MASK;
		v4l2_dbg(1, debug, sd, "%s: %#x %d\n", __func__, mask, en);
		rk628_i2c_update_bits(bt1120->rk628, HDMI_RX_DMI_DISABLE_IF,
				   mask, HDMI_ENABLE(0) | AUD_ENABLE(0));
	}
}
static int rk628_bt1120_get_detected_timings(struct v4l2_subdev *sd,
					     struct v4l2_dv_timings *timings)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);
	struct v4l2_bt_timings *bt = &timings->bt;
	u32 hact, vact, htotal, vtotal, fps, status;
	u32 val;
	u32 modetclk_cnt_hs, modetclk_cnt_vs, hs, vs;
	u32 hofs_pix, hbp, hfp, vbp, vfp;
	u32 tmds_clk, tmdsclk_cnt;
	u64 tmp_data;
	int retry = 0;

__retry:
	memset(timings, 0, sizeof(struct v4l2_dv_timings));
	timings->type = V4L2_DV_BT_656_1120;
	rk628_i2c_read(bt1120->rk628, HDMI_RX_SCDC_REGS1, &val);
	status = val;

	rk628_i2c_read(bt1120->rk628, HDMI_RX_MD_STS, &val);
	bt->interlaced = val & ILACE_STS ?
		V4L2_DV_INTERLACED : V4L2_DV_PROGRESSIVE;

	rk628_i2c_read(bt1120->rk628, HDMI_RX_MD_HACT_PX, &val);
	hact = val & 0xffff;
	rk628_i2c_read(bt1120->rk628, HDMI_RX_MD_VAL, &val);
	vact = val & 0xffff;
	rk628_i2c_read(bt1120->rk628, HDMI_RX_MD_HT1, &val);
	htotal = (val >> 16) & 0xffff;
	rk628_i2c_read(bt1120->rk628, HDMI_RX_MD_VTL, &val);
	vtotal = val & 0xffff;
	rk628_i2c_read(bt1120->rk628, HDMI_RX_MD_HT1, &val);
	hofs_pix = val & 0xffff;
	rk628_i2c_read(bt1120->rk628, HDMI_RX_MD_VOL, &val);
	vbp = (val & 0xffff) + 1;

	rk628_i2c_read(bt1120->rk628, HDMI_RX_HDMI_CKM_RESULT, &val);
	tmdsclk_cnt = val & 0xffff;
	tmp_data = tmdsclk_cnt;
	tmp_data = ((tmp_data * MODETCLK_HZ) + MODETCLK_CNT_NUM / 2);
	do_div(tmp_data, MODETCLK_CNT_NUM);
	tmds_clk = tmp_data;
	if (!htotal || !vtotal) {
		v4l2_err(&bt1120->sd, "timing err, htotal:%d, vtotal:%d\n",
				htotal, vtotal);
		if (retry++ < 5)
			goto __retry;

		goto TIMING_ERR;
	}
	fps = (tmds_clk + (htotal * vtotal) / 2) / (htotal * vtotal);

	rk628_i2c_read(bt1120->rk628, HDMI_RX_MD_HT0, &val);
	modetclk_cnt_hs = val & 0xffff;
	hs = (tmdsclk_cnt * modetclk_cnt_hs + MODETCLK_CNT_NUM / 2) /
		MODETCLK_CNT_NUM;

	rk628_i2c_read(bt1120->rk628, HDMI_RX_MD_VSC, &val);
	modetclk_cnt_vs = val & 0xffff;
	vs = (tmdsclk_cnt * modetclk_cnt_vs + MODETCLK_CNT_NUM / 2) /
		MODETCLK_CNT_NUM;
	vs = (vs + htotal / 2) / htotal;

	if ((hofs_pix < hs) || (htotal < (hact + hofs_pix)) ||
			(vtotal < (vact + vs + vbp))) {
		v4l2_err(sd, "timing err, total:%dx%d, act:%dx%d, hofs:%d, hs:%d, vs:%d, vbp:%d\n",
			 htotal, vtotal, hact, vact, hofs_pix, hs, vs, vbp);
		goto TIMING_ERR;
	}
	hbp = hofs_pix - hs;
	hfp = htotal - hact - hofs_pix;
	vfp = vtotal - vact - vs - vbp;

	v4l2_dbg(2, debug, sd, "cnt_num:%d, tmds_cnt:%d, hs_cnt:%d, vs_cnt:%d, hofs:%d\n",
		MODETCLK_CNT_NUM, tmdsclk_cnt, modetclk_cnt_hs, modetclk_cnt_vs, hofs_pix);

	bt->width = hact;
	bt->height = vact;
	bt->hfrontporch = hfp;
	bt->hsync = hs;
	bt->hbackporch = hbp;
	bt->vfrontporch = vfp;
	bt->vsync = vs;
	bt->vbackporch = vbp;
	bt->pixelclock = htotal * vtotal * fps;

	if (bt->interlaced == V4L2_DV_INTERLACED) {
		bt->height *= 2;
		bt->il_vsync = bt->vsync + 1;
		bt->pixelclock /= 2;
	}

	v4l2_dbg(1, debug, sd, "SCDC_REGS1:%#x, act:%dx%d, total:%dx%d, fps:%d, pixclk:%llu\n",
		 status, hact, vact, htotal, vtotal, fps, bt->pixelclock);
	v4l2_dbg(1, debug, sd, "hfp:%d, hs:%d, hbp:%d, vfp:%d, vs:%d, vbp:%d, interlace:%d\n",
		 bt->hfrontporch, bt->hsync, bt->hbackporch, bt->vfrontporch,
		 bt->vsync, bt->vbackporch, bt->interlaced);

	bt1120->src_timings = *timings;
	if (bt1120->scaler_en)
		*timings = bt1120->timings;

	return 0;

TIMING_ERR:
	return -ENOLCK;
}

static void rk628_hdmirx_config_all(struct v4l2_subdev *sd)
{
	int ret;
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);

	rk628_hdmirx_controller_setup(bt1120->rk628);
	ret = rk628_hdmirx_phy_setup(sd);
	if (ret >= 0) {
		rk628_bt1120_format_change(sd);
		bt1120->nosignal = false;
	}
}

static void rk628_hdmirx_reset_regfile(struct v4l2_subdev *sd)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);

	rk628_control_assert(bt1120->rk628, RGU_REGFILE);
	udelay(10);
	rk628_control_deassert(bt1120->rk628, RGU_REGFILE);
	udelay(10);

	rk628_cru_initialize(bt1120->rk628);
	rk628_bt1120_initial_setup(sd);
	if (tx_5v_power_present(sd))
		rk628_hdmirx_hpd_ctrl(sd, true);
}

static void rk628_set_io_func_to_vop(struct rk628 *rk628)
{
	/* pinctrl for vop pin */
	rk628_i2c_write(rk628, GRF_GPIO2AB_SEL_CON, 0xffffffff);
	rk628_i2c_write(rk628, GRF_GPIO2C_SEL_CON, 0xffff5555);
	rk628_i2c_write(rk628, GRF_GPIO3AB_SEL_CON, 0x10b010b);
}

static void rk628_set_io_func_to_gpio(struct rk628 *rk628)
{
	/* pinctrl for gpio */
	rk628_i2c_write(rk628, GRF_GPIO2AB_SEL_CON, 0xffff0000);
	rk628_i2c_write(rk628, GRF_GPIO2C_SEL_CON, 0xffff0000);
	rk628_i2c_write(rk628, GRF_GPIO3AB_SEL_CON, 0x0fff0000);
}

static void rk628_bt1120_delayed_work_enable_hotplug(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct rk628_bt1120 *bt1120 = container_of(dwork, struct rk628_bt1120,
			delayed_work_enable_hotplug);
	struct v4l2_subdev *sd = &bt1120->sd;
	bool plugin;

	mutex_lock(&bt1120->confctl_mutex);
	bt1120->avi_rcv_rdy = false;
	plugin = tx_5v_power_present(sd);
	v4l2_dbg(1, debug, sd, "%s: 5v_det:%d\n", __func__, plugin);
	if (plugin) {
		rk628_set_io_func_to_vop(bt1120->rk628);
		rk628_bt1120_enable_interrupts(sd, false);
		rk628_hdmirx_audio_setup(bt1120->audio_info);
		rk628_hdmirx_set_hdcp(bt1120->rk628, &bt1120->hdcp, bt1120->enable_hdcp);
		rk628_hdmirx_hpd_ctrl(sd, true);
		rk628_hdmirx_config_all(sd);
		rk628_bt1120_enable_interrupts(sd, true);
		rk628_i2c_update_bits(bt1120->rk628, GRF_SYSTEM_CON0,
				SW_I2S_DATA_OEN_MASK, SW_I2S_DATA_OEN(0));
	} else {
		rk628_bt1120_enable_interrupts(sd, false);
		enable_stream(sd, false);
		cancel_delayed_work(&bt1120->delayed_work_res_change);
		rk628_hdmirx_audio_cancel_work_audio(bt1120->audio_info, true);
		rk628_hdmirx_hpd_ctrl(sd, false);
		rk628_hdmirx_phy_power_off(sd);
		rk628_hdmirx_controller_reset(sd);
		bt1120->nosignal = true;
		rk628_set_io_func_to_gpio(bt1120->rk628);
	}
	mutex_unlock(&bt1120->confctl_mutex);
}

static int rk628_check_resulotion_change(struct v4l2_subdev *sd)
{
	u32 val;
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);
	u32 htotal, vtotal;
	u32 old_htotal, old_vtotal;
	struct v4l2_bt_timings *bt = &bt1120->src_timings.bt;

	rk628_i2c_read(bt1120->rk628, HDMI_RX_MD_HT1, &val);
	htotal = (val >> 16) & 0xffff;
	rk628_i2c_read(bt1120->rk628, HDMI_RX_MD_VTL, &val);
	vtotal = val & 0xffff;

	old_htotal = bt->hfrontporch + bt->hsync + bt->width + bt->hbackporch;
	old_vtotal = bt->vfrontporch + bt->vsync + bt->height + bt->vbackporch;

	v4l2_dbg(1, debug, sd, "new mode: %d x %d\n", htotal, vtotal);
	v4l2_dbg(1, debug, sd, "old mode: %d x %d\n", old_htotal, old_vtotal);

	if (htotal != old_htotal || vtotal != old_vtotal)
		return 1;

	return 0;
}

static void rk628_delayed_work_res_change(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct rk628_bt1120 *bt1120 = container_of(dwork, struct rk628_bt1120,
			delayed_work_res_change);
	struct v4l2_subdev *sd = &bt1120->sd;
	bool plugin;

	mutex_lock(&bt1120->confctl_mutex);
	bt1120->avi_rcv_rdy = false;
	plugin = tx_5v_power_present(sd);
	v4l2_dbg(1, debug, sd, "%s: 5v_det:%d\n", __func__, plugin);
	if (plugin) {
		if (rk628_check_resulotion_change(sd)) {
			v4l2_dbg(1, debug, sd, "res change, recfg ctrler and phy!\n");

			rk628_bt1120_enable_interrupts(sd, false);
			enable_stream(sd, false);
			cancel_delayed_work(&bt1120->delayed_work_res_change);
			rk628_hdmirx_audio_cancel_work_audio(bt1120->audio_info, true);
			rk628_hdmirx_hpd_ctrl(sd, false);
			rk628_hdmirx_phy_power_off(sd);
			rk628_hdmirx_controller_reset(sd);
			bt1120->nosignal = true;
			rk628_hdmirx_reset_regfile(sd);
		} else {
			rk628_bt1120_format_change(sd);
			bt1120->nosignal = false;
			rk628_bt1120_enable_interrupts(sd, true);
		}
	}
	mutex_unlock(&bt1120->confctl_mutex);
}

static void rk628_hdmirx_hpd_ctrl(struct v4l2_subdev *sd, bool en)
{
	u8 en_level, set_level;
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);

	v4l2_dbg(1, debug, sd, "%s: %sable, hpd invert:%d\n", __func__,
		 en ? "en" : "dis", bt1120->hpd_output_inverted);
	en_level = bt1120->hpd_output_inverted ? 0 : 1;
	set_level = en ? en_level : !en_level;
	rk628_i2c_update_bits(bt1120->rk628, HDMI_RX_HDMI_SETUP_CTRL,
			HOT_PLUG_DETECT_MASK, HOT_PLUG_DETECT(set_level));
}

static int rk628_bt1120_s_ctrl_detect_tx_5v(struct v4l2_subdev *sd)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);

	return v4l2_ctrl_s_ctrl(bt1120->detect_tx_5v_ctrl,
			tx_5v_power_present(sd));
}

static int rk628_bt1120_s_ctrl_audio_sampling_rate(struct v4l2_subdev *sd)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);

	return v4l2_ctrl_s_ctrl(bt1120->audio_sampling_rate_ctrl,
			get_audio_sampling_rate(sd));
}

static int rk628_bt1120_s_ctrl_audio_present(struct v4l2_subdev *sd)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);

	return v4l2_ctrl_s_ctrl(bt1120->audio_present_ctrl,
			audio_present(sd));
}

static int rk628_bt1120_update_controls(struct v4l2_subdev *sd)
{
	int ret = 0;

	ret |= rk628_bt1120_s_ctrl_detect_tx_5v(sd);
	ret |= rk628_bt1120_s_ctrl_audio_sampling_rate(sd);
	ret |= rk628_bt1120_s_ctrl_audio_present(sd);

	return ret;
}

static void enable_bt1120tx(struct v4l2_subdev *sd)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);
	u8 video_fmt;
	u32 val = 0;
	int avi_rdy;

	rk628_post_process_setup(sd);

	rk628_i2c_update_bits(bt1120->rk628, GRF_POST_PROC_CON,
			   SW_DCLK_OUT_INV_EN, SW_DCLK_OUT_INV_EN);

	if (bt1120->dual_edge) {
		val |= ENC_DUALEDGE_EN(1);
		rk628_i2c_write(bt1120->rk628, GRF_BT1120_DCLK_DELAY_CON0, 0x10000000);
		rk628_i2c_write(bt1120->rk628, GRF_BT1120_DCLK_DELAY_CON1, 0);
	}

	val |= BT1120_UV_SWAP(0);
	rk628_i2c_write(bt1120->rk628, GRF_RGB_ENC_CON, val);
	v4l2_dbg(1, debug, sd, "%s bt1120 cofig done\n", __func__);

	avi_rdy = rk628_is_avi_ready(bt1120->rk628, bt1120->avi_rcv_rdy);

	rk628_i2c_read(bt1120->rk628, HDMI_RX_PDEC_AVI_PB, &val);
	video_fmt = (val & VIDEO_FORMAT_MASK) >> 5;
	v4l2_dbg(1, debug, &bt1120->sd, "%s PDEC_AVI_PB:%#x, video format:%d\n",
			__func__, val, video_fmt);
	if (video_fmt) {
		/* yuv data: cfg SW_YUV2VYU_SWP */
		rk628_i2c_write(bt1120->rk628, GRF_CSC_CTRL_CON,
				SW_YUV2VYU_SWP(1) |
				SW_R2Y_EN(0));
	} else {
		/* rgb data: cfg SW_R2Y_EN */
		rk628_i2c_write(bt1120->rk628, GRF_CSC_CTRL_CON,
				SW_YUV2VYU_SWP(0) |
				SW_R2Y_EN(1));
	}

	/* if avi packet is not stable, reset ctrl*/
	if (!avi_rdy)
		schedule_delayed_work(&bt1120->delayed_work_enable_hotplug, HZ / 20);
}

static void enable_stream(struct v4l2_subdev *sd, bool en)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);

	v4l2_dbg(1, debug, sd, "%s: %sable\n", __func__, en ? "en" : "dis");
	if (en) {
		rk628_hdmirx_vid_enable(sd, true);
		enable_bt1120tx(sd);
	} else {
		rk628_i2c_write(bt1120->rk628, GRF_SCALER_CON0, SCL_EN(0));
		rk628_hdmirx_vid_enable(sd, false);
	}
}

static void rk628_post_process_setup(struct v4l2_subdev *sd)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);
	struct v4l2_bt_timings *bt = &bt1120->src_timings.bt;
	struct v4l2_bt_timings *dst_bt = &bt1120->timings.bt;
	struct videomode src, dst;
	u64 dst_pclk;

	src.hactive = bt->width;
	src.hfront_porch = bt->hfrontporch;
	src.hsync_len = bt->hsync;
	src.hback_porch = bt->hbackporch;
	src.vactive = bt->height;
	src.vfront_porch = bt->vfrontporch;
	src.vsync_len = bt->vsync;
	src.vback_porch = bt->vbackporch;
	src.pixelclock = bt->pixelclock;
	src.flags = 0;
	if (bt->interlaced == V4L2_DV_INTERLACED)
		src.flags |= DISPLAY_FLAGS_INTERLACED;
	if (!src.pixelclock) {
		enable_stream(sd, false);
		bt1120->nosignal = true;
		schedule_delayed_work(&bt1120->delayed_work_enable_hotplug, HZ / 20);
		return;
	}

	dst.hactive = dst_bt->width;
	dst.hfront_porch = dst_bt->hfrontporch;
	dst.hsync_len = dst_bt->hsync;
	dst.hback_porch = dst_bt->hbackporch;
	dst.vactive = dst_bt->height;
	dst.vfront_porch = dst_bt->vfrontporch;
	dst.vsync_len = dst_bt->vsync;
	dst.vback_porch = dst_bt->vbackporch;
	dst.pixelclock = dst_bt->pixelclock;

	rk628_post_process_en(bt1120->rk628, &src, &dst, &dst_pclk);
	dst_bt->pixelclock = dst_pclk;
}

static int rk628_hdmirx_phy_power_on(struct v4l2_subdev *sd)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);
	int ret, f;

	/* Bit31 is used to distinguish HDMI cable mode and direct connection
	 * mode in the rk628_combrxphy driver.
	 * Bit31: 0 -direct connection mode;
	 *        1 -cable mode;
	 * The cable mode is to know the input clock frequency through cdr_mode
	 * in the rk628_combrxphy driver, and the cable mode supports up to
	 * 297M, so 297M is passed uniformly here.
	 */
	f = 297000 | BIT(31);

	if (bt1120->rxphy_pwron) {
		v4l2_dbg(1, debug, sd, "rxphy already power on, power off!\n");
		ret = rk628_rxphy_power_off(bt1120->rk628);
		if (ret)
			v4l2_err(sd, "hdmi rxphy power off failed!\n");
		else
			bt1120->rxphy_pwron = false;
		usleep_range(100, 110);
	}

	if (bt1120->rxphy_pwron == false) {
		rk628_hdmirx_ctrl_enable(sd, 0);
		ret = rk628_rxphy_power_on(bt1120->rk628, f);
		if (ret) {
			bt1120->rxphy_pwron = false;
			v4l2_err(sd, "hdmi rxphy power on failed\n");
		} else {
			bt1120->rxphy_pwron = true;
		}
		rk628_hdmirx_ctrl_enable(sd, 1);
		msleep(100);
	}

	return ret;
}

static int rk628_hdmirx_phy_power_off(struct v4l2_subdev *sd)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);

	if (bt1120->rxphy_pwron) {
		v4l2_dbg(1, debug, sd, "rxphy power off!\n");
		rk628_rxphy_power_off(bt1120->rk628);
		bt1120->rxphy_pwron = false;
	}
	usleep_range(100, 100);

	return 0;
}

static void rk628_hdmirx_vid_enable(struct v4l2_subdev *sd, bool en)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);

	v4l2_dbg(1, debug, sd, "%s: %sable\n", __func__, en ? "en" : "dis");
	if (en) {
		if (!bt1120->i2s_enable_default)
			rk628_hdmirx_audio_i2s_ctrl(bt1120->audio_info, true);
		rk628_i2c_update_bits(bt1120->rk628, HDMI_RX_DMI_DISABLE_IF,
				      VID_ENABLE_MASK, VID_ENABLE(1));
	} else {
		if (!bt1120->i2s_enable_default)
			rk628_hdmirx_audio_i2s_ctrl(bt1120->audio_info, false);
		rk628_i2c_update_bits(bt1120->rk628, HDMI_RX_DMI_DISABLE_IF,
				      VID_ENABLE_MASK, VID_ENABLE(0));
	}
}

static void rk628_hdmirx_controller_reset(struct v4l2_subdev *sd)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);

	rk628_control_assert(bt1120->rk628, RGU_HDMIRX_PON);
	udelay(10);
	rk628_control_deassert(bt1120->rk628, RGU_HDMIRX_PON);
	udelay(10);
	rk628_i2c_write(bt1120->rk628, HDMI_RX_DMI_SW_RST, 0x000101ff);
	rk628_i2c_write(bt1120->rk628, HDMI_RX_DMI_DISABLE_IF, 0x00000000);
	rk628_i2c_write(bt1120->rk628, HDMI_RX_DMI_DISABLE_IF, 0x0000017f);
	rk628_i2c_write(bt1120->rk628, HDMI_RX_DMI_DISABLE_IF, 0x0001017f);
}

static bool rk628_rcv_supported_res(struct v4l2_subdev *sd, u32 width,
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

static int rk628_hdmirx_phy_setup(struct v4l2_subdev *sd)
{
	u32 i, cnt, val;
	u32 width, height, frame_width, frame_height, status;
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);
	int ret;

	for (i = 0; i < RXPHY_CFG_MAX_TIMES; i++) {
		ret = rk628_hdmirx_phy_power_on(sd);
		if (ret < 0) {
			msleep(50);
			continue;
		}
		cnt = 0;

		do {
			cnt++;
			rk628_i2c_read(bt1120->rk628, HDMI_RX_MD_HACT_PX, &val);
			width = val & 0xffff;
			rk628_i2c_read(bt1120->rk628, HDMI_RX_MD_VAL, &val);
			height = val & 0xffff;
			rk628_i2c_read(bt1120->rk628, HDMI_RX_MD_HT1, &val);
			frame_width = (val >> 16) & 0xffff;
			rk628_i2c_read(bt1120->rk628, HDMI_RX_MD_VTL, &val);
			frame_height = val & 0xffff;
			rk628_i2c_read(bt1120->rk628, HDMI_RX_SCDC_REGS1, &val);
			status = val;
			v4l2_dbg(1, debug, sd, "%s read wxh:%dx%d, total:%dx%d, SCDC_REGS1:%#x, cnt:%d\n",
				 __func__, width, height, frame_width, frame_height, status, cnt);

			rk628_i2c_read(bt1120->rk628, HDMI_RX_PDEC_STS, &val);
			if (val & DVI_DET)
				dev_info(bt1120->dev, "DVI mode detected\n");

			if (!tx_5v_power_present(sd)) {
				v4l2_info(sd, "HDMI pull out, return!\n");
				return -1;
			}

			if (cnt >= 15)
				break;
		} while (((status & 0xfff) != 0xf00) ||
				(!rk628_rcv_supported_res(sd, width, height)));

		if (((status & 0xfff) != 0xf00) ||
				(!rk628_rcv_supported_res(sd, width, height))) {
			v4l2_err(sd, "%s hdmi rxphy lock failed, retry:%d\n",
					__func__, i);
			continue;
		} else {
			break;
		}
	}

	if (i == RXPHY_CFG_MAX_TIMES)
		return -1;

	return 0;
}

static void rk628_bt1120_initial_setup(struct v4l2_subdev *sd)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);
	struct v4l2_subdev_edid def_edid;

	/* selete int io function */
	rk628_i2c_write(bt1120->rk628, GRF_GPIO3AB_SEL_CON, 0x30002000);
	rk628_i2c_write(bt1120->rk628, GRF_GPIO1AB_SEL_CON, HIWORD_UPDATE(0x7, 10, 8));
	/* I2S_SCKM0 */
	rk628_i2c_write(bt1120->rk628, GRF_GPIO0AB_SEL_CON, HIWORD_UPDATE(0x1, 2, 2));
	/* I2SLR_M0 */
	rk628_i2c_write(bt1120->rk628, GRF_GPIO0AB_SEL_CON, HIWORD_UPDATE(0x1, 3, 3));
	/* I2SM0D0 */
	rk628_i2c_write(bt1120->rk628, GRF_GPIO0AB_SEL_CON, HIWORD_UPDATE(0x1, 5, 4));
	/* hdmirx int en */
	rk628_i2c_write(bt1120->rk628, GRF_INTR0_EN, 0x01000100);

	udelay(10);
	rk628_control_assert(bt1120->rk628, RGU_HDMIRX);
	rk628_control_assert(bt1120->rk628, RGU_HDMIRX_PON);
	rk628_control_assert(bt1120->rk628, RGU_BT1120DEC);
	udelay(10);
	rk628_control_deassert(bt1120->rk628, RGU_HDMIRX);
	rk628_control_deassert(bt1120->rk628, RGU_HDMIRX_PON);
	rk628_control_deassert(bt1120->rk628, RGU_BT1120DEC);
	udelay(10);

	rk628_i2c_update_bits(bt1120->rk628, GRF_SYSTEM_CON0,
			SW_BT_DATA_OEN_MASK |
			SW_INPUT_MODE_MASK |
			SW_OUTPUT_MODE_MASK |
			SW_EFUSE_HDCP_EN_MASK |
			SW_HSYNC_POL_MASK |
			SW_VSYNC_POL_MASK,
			SW_INPUT_MODE(INPUT_MODE_HDMI) |
			SW_OUTPUT_MODE(OUTPUT_MODE_BT1120) |
			SW_EFUSE_HDCP_EN(0) |
			SW_HSYNC_POL(1) |
			SW_VSYNC_POL(1));
	rk628_hdmirx_controller_reset(sd);

	def_edid.pad = 0;
	def_edid.start_block = 0;
	def_edid.blocks = 2;
	def_edid.edid = edid_init_data;
	rk628_bt1120_s_edid(sd, &def_edid);
	rk628_hdmirx_set_hdcp(bt1120->rk628, &bt1120->hdcp, false);

	if (tx_5v_power_present(sd))
		schedule_delayed_work(&bt1120->delayed_work_enable_hotplug, 1000);
}

static void rk628_bt1120_format_change(struct v4l2_subdev *sd)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);
	struct v4l2_dv_timings timings;
	const struct v4l2_event rk628_bt1120_ev_fmt = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
	};

	rk628_bt1120_get_detected_timings(sd, &timings);
	if (!v4l2_match_dv_timings(&bt1120->timings, &timings, 0, false)) {
		/* automatically set timing rather than set by userspace */
		rk628_bt1120_s_dv_timings(sd, &timings);
		v4l2_print_dv_timings(sd->name,
				"rk628_bt1120_format_change: New format: ",
				&timings, false);
	}

	if (sd->devnode)
		v4l2_subdev_notify_event(sd, &rk628_bt1120_ev_fmt);
}

static void rk628_bt1120_enable_interrupts(struct v4l2_subdev *sd, bool en)
{
	u32 pdec_ien, md_ien;
	u32 pdec_mask = 0, md_mask = 0;
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);

	pdec_mask |= AVI_RCV_ENSET;
	md_mask = VACT_LIN_ENSET | HACT_PIX_ENSET | HS_CLK_ENSET |
		  DE_ACTIVITY_ENSET | VS_ACT_ENSET | HS_ACT_ENSET;
	v4l2_dbg(1, debug, sd, "%s: %sable\n", __func__, en ? "en" : "dis");
	/* clr irq */
	rk628_i2c_write(bt1120->rk628, HDMI_RX_MD_ICLR, md_mask);
	rk628_i2c_write(bt1120->rk628, HDMI_RX_PDEC_ICLR, pdec_mask);
	if (en) {
		rk628_i2c_write(bt1120->rk628, HDMI_RX_MD_IEN_SET, md_mask);
		rk628_i2c_write(bt1120->rk628, HDMI_RX_PDEC_IEN_SET, pdec_mask);
		bt1120->vid_ints_en = true;
	} else {
		rk628_i2c_write(bt1120->rk628, HDMI_RX_MD_IEN_CLR, md_mask);
		rk628_i2c_write(bt1120->rk628, HDMI_RX_PDEC_IEN_CLR, pdec_mask);
		rk628_i2c_write(bt1120->rk628, HDMI_RX_AUD_FIFO_IEN_CLR, 0x1f);
		bt1120->vid_ints_en = false;
	}
	usleep_range(5000, 5000);
	rk628_i2c_read(bt1120->rk628, HDMI_RX_MD_IEN, &md_ien);
	rk628_i2c_read(bt1120->rk628, HDMI_RX_PDEC_IEN, &pdec_ien);
	v4l2_dbg(1, debug, sd, "%s MD_IEN:%#x, PDEC_IEN:%#x\n", __func__, md_ien, pdec_ien);
}

static int rk628_bt1120_isr(struct v4l2_subdev *sd, u32 status, bool *handled)
{
	u32 md_ints, pdec_ints, fifo_ints, hact, vact;
	bool plugin;
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);
	void *audio_info = bt1120->audio_info;

	if (handled == NULL) {
		v4l2_err(sd, "handled NULL, err return!\n");
		return -EINVAL;
	}
	rk628_i2c_read(bt1120->rk628, HDMI_RX_PDEC_ISTS, &pdec_ints);
	if (rk628_audio_ctsnints_enabled(audio_info)) {
		if (pdec_ints & (ACR_N_CHG_ICLR | ACR_CTS_CHG_ICLR)) {
			rk628_csi_isr_ctsn(audio_info, pdec_ints);
			pdec_ints &= ~(ACR_CTS_CHG_ICLR | ACR_CTS_CHG_ICLR);
			*handled = true;
		}
	}
	if (rk628_audio_fifoints_enabled(audio_info)) {
		rk628_i2c_read(bt1120->rk628, HDMI_RX_AUD_FIFO_ISTS, &fifo_ints);
		if (fifo_ints & 0x18) {
			rk628_csi_isr_fifoints(audio_info, fifo_ints);
			*handled = true;
		}
	}
	if (bt1120->vid_ints_en) {
		rk628_i2c_read(bt1120->rk628, HDMI_RX_MD_ISTS, &md_ints);
		plugin = tx_5v_power_present(sd);
		v4l2_dbg(1, debug, sd, "%s: md_ints: %#x, pdec_ints:%#x, plugin: %d\n",
			 __func__, md_ints, pdec_ints, plugin);

		if ((md_ints & (VACT_LIN_ISTS | HACT_PIX_ISTS |
				HS_CLK_ISTS | DE_ACTIVITY_ISTS |
				VS_ACT_ISTS | HS_ACT_ISTS))
				&& plugin) {

			rk628_i2c_read(bt1120->rk628, HDMI_RX_MD_HACT_PX, &hact);
			rk628_i2c_read(bt1120->rk628, HDMI_RX_MD_VAL, &vact);
			v4l2_dbg(1, debug, sd, "%s: HACT:%#x, VACT:%#x\n",
				 __func__, hact, vact);

			rk628_bt1120_enable_interrupts(sd, false);
			enable_stream(sd, false);
			bt1120->nosignal = true;
			schedule_delayed_work(&bt1120->delayed_work_res_change, HZ / 2);

			v4l2_dbg(1, debug, sd, "%s: hact/vact change, md_ints: %#x\n",
				 __func__, (u32)(md_ints & (VACT_LIN_ISTS | HACT_PIX_ISTS)));
			*handled = true;
		}

		if ((pdec_ints & AVI_RCV_ISTS) && plugin && !bt1120->avi_rcv_rdy) {
			v4l2_dbg(1, debug, sd, "%s: AVI RCV INT!\n", __func__);
			bt1120->avi_rcv_rdy = true;
			/* After get the AVI_RCV interrupt state, disable interrupt. */
			rk628_i2c_write(bt1120->rk628, HDMI_RX_PDEC_IEN_CLR, AVI_RCV_ISTS);

			*handled = true;
		}
	}
	if (*handled != true)
		v4l2_dbg(1, debug, sd, "%s: unhandled interrupt!\n", __func__);

	/* clear interrupts */
	rk628_i2c_write(bt1120->rk628, HDMI_RX_MD_ICLR, 0xffffffff);
	rk628_i2c_write(bt1120->rk628, HDMI_RX_PDEC_ICLR, 0xffffffff);
	rk628_i2c_write(bt1120->rk628, GRF_INTR0_CLR_EN, 0x01000100);

	return 0;
}

static irqreturn_t rk628_bt1120_irq_handler(int irq, void *dev_id)
{
	struct rk628_bt1120 *bt1120 = dev_id;
	bool handled = false;

	rk628_bt1120_isr(&bt1120->sd, 0, &handled);

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

static void rk628_bt1120_irq_poll_timer(struct timer_list *t)
{
	struct rk628_bt1120 *bt1120 = from_timer(bt1120, t, timer);

	schedule_work(&bt1120->work_i2c_poll);
	mod_timer(&bt1120->timer, jiffies + msecs_to_jiffies(POLL_INTERVAL_MS));
}

static void rk628_bt1120_work_i2c_poll(struct work_struct *work)
{
	struct rk628_bt1120 *bt1120 = container_of(work, struct rk628_bt1120,
			work_i2c_poll);
	struct v4l2_subdev *sd = &bt1120->sd;

	rk628_bt1120_format_change(sd);
}

static int rk628_bt1120_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
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

static int rk628_bt1120_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);
	static u8 cnt;

	*status = 0;
	*status |= no_signal(sd) ? V4L2_IN_ST_NO_SIGNAL : 0;

	if (no_signal(sd) && tx_5v_power_present(sd)) {
		if (cnt++ >= 6) {
			cnt = 0;
			v4l2_info(sd, "no signal but 5v_det, recfg hdmirx!\n");
			schedule_delayed_work(&bt1120->delayed_work_enable_hotplug,
					HZ / 20);
		}
	} else {
		cnt = 0;
	}

	v4l2_dbg(1, debug, sd, "%s: status = 0x%x\n", __func__, *status);

	return 0;
}

static int rk628_bt1120_s_dv_timings(struct v4l2_subdev *sd,
		struct v4l2_dv_timings *timings)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);

	if (!timings)
		return -EINVAL;

	if (debug)
		v4l2_print_dv_timings(sd->name, "rk628_bt1120_s_dv_timings: ",
				timings, false);

	if (v4l2_match_dv_timings(&bt1120->timings, timings, 0, false)) {
		v4l2_dbg(1, debug, sd, "%s: no change\n", __func__);
		return 0;
	}

	if (!v4l2_valid_dv_timings(timings, &rk628_bt1120_timings_cap, NULL,
				NULL)) {
		v4l2_dbg(1, debug, sd, "%s: timings out of range\n", __func__);
		return -ERANGE;
	}

	bt1120->timings = *timings;
	enable_stream(sd, false);

	return 0;
}

static int rk628_bt1120_g_dv_timings(struct v4l2_subdev *sd,
		struct v4l2_dv_timings *timings)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);

	*timings = bt1120->timings;

	return 0;
}

static int rk628_bt1120_enum_dv_timings(struct v4l2_subdev *sd,
		struct v4l2_enum_dv_timings *timings)
{
	if (timings->pad != 0)
		return -EINVAL;

	return v4l2_enum_dv_timings_cap(timings, &rk628_bt1120_timings_cap, NULL,
			NULL);
}

static int rk628_bt1120_query_dv_timings(struct v4l2_subdev *sd,
		struct v4l2_dv_timings *timings)
{
	int ret;

	ret = rk628_bt1120_get_detected_timings(sd, timings);
	if (ret)
		return ret;

	if (debug)
		v4l2_print_dv_timings(sd->name, "rk628_bt1120_query_dv_timings: ",
				timings, false);

	if (!v4l2_valid_dv_timings(timings, &rk628_bt1120_timings_cap, NULL,
				NULL)) {
		v4l2_dbg(1, debug, sd, "%s: timings out of range\n", __func__);
		return -ERANGE;
	}

	return 0;
}

static int rk628_bt1120_dv_timings_cap(struct v4l2_subdev *sd,
		struct v4l2_dv_timings_cap *cap)
{
	if (cap->pad != 0)
		return -EINVAL;

	*cap = rk628_bt1120_timings_cap;

	return 0;
}

static int rk628_bt1120_g_mbus_config(struct v4l2_subdev *sd,
				      unsigned int pad,
				      struct v4l2_mbus_config *cfg)
{
	cfg->type = V4L2_MBUS_BT656;
	cfg->flags = V4L2_MBUS_HSYNC_ACTIVE_HIGH |
				V4L2_MBUS_VSYNC_ACTIVE_HIGH |
				V4L2_MBUS_PCLK_SAMPLE_RISING;

	return 0;
}

static int rk628_bt1120_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);

	mutex_lock(&bt1120->confctl_mutex);
	enable_stream(sd, enable);
	mutex_unlock(&bt1120->confctl_mutex);

	return 0;
}

static int rk628_bt1120_enum_mbus_code(struct v4l2_subdev *sd,
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

static int rk628_bt1120_get_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret = -1;
	struct rk628_bt1120 *bt1120 = container_of(ctrl->handler, struct rk628_bt1120,
			hdl);
	struct v4l2_subdev *sd = &(bt1120->sd);

	if (ctrl->id == V4L2_CID_DV_RX_POWER_PRESENT) {
		ret = tx_5v_power_present(sd);
		*ctrl->p_new.p_s32 = ret;
	}

	return ret;
}

static int rk628_bt1120_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
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

static int rk628_bt1120_enum_frame_interval(struct v4l2_subdev *sd,
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

static int rk628_bt1120_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);

	mutex_lock(&bt1120->confctl_mutex);
	format->format.code = bt1120->mbus_fmt_code;
	format->format.width = bt1120->timings.bt.width;
	format->format.height = bt1120->timings.bt.height;
	format->format.field = bt1120->timings.bt.interlaced ?
		V4L2_FIELD_INTERLACED : V4L2_FIELD_NONE;
	mutex_unlock(&bt1120->confctl_mutex);

	v4l2_dbg(1, debug, sd, "%s: fmt code:%d, w:%d, h:%d, field code:%d\n",
			__func__, format->format.code, format->format.width,
			format->format.height, format->format.field);

	return 0;
}

static int rk628_bt1120_get_reso_dist(const struct rk628_bt1120_mode *mode,
		struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct rk628_bt1120_mode *
rk628_bt1120_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = rk628_bt1120_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int rk628_bt1120_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);
	const struct rk628_bt1120_mode *mode;

	u32 code = format->format.code; /* is overwritten by get_fmt */
	int ret = rk628_bt1120_get_fmt(sd, cfg, format);

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

	bt1120->mbus_fmt_code = format->format.code;
	mode = rk628_bt1120_find_best_fit(format);
	bt1120->cur_mode = mode;

	v4l2_dbg(1, debug, sd,
		"%s res wxh:%dx%d, link freq:%llu, pixrate:%u\n",
		__func__, mode->width, mode->height,
		link_freq_menu_items[0], RK628_CSI_PIXEL_RATE_LOW);
	__v4l2_ctrl_s_ctrl(bt1120->link_freq, 0);
	__v4l2_ctrl_s_ctrl_int64(bt1120->pixel_rate,
		RK628_CSI_PIXEL_RATE_LOW);

	mutex_lock(&bt1120->confctl_mutex);
	enable_stream(sd, false);
	mutex_unlock(&bt1120->confctl_mutex);

	return 0;
}

static int rk628_bt1120_g_edid(struct v4l2_subdev *sd,
		struct v4l2_subdev_edid *edid)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);
	u32 i, val;

	memset(edid->reserved, 0, sizeof(edid->reserved));

	if (edid->pad != 0)
		return -EINVAL;

	if (edid->start_block == 0 && edid->blocks == 0) {
		edid->blocks = bt1120->edid_blocks_written;
		return 0;
	}

	if (bt1120->edid_blocks_written == 0)
		return -ENODATA;

	if (edid->start_block >= bt1120->edid_blocks_written ||
			edid->blocks == 0)
		return -EINVAL;

	if (edid->start_block + edid->blocks > bt1120->edid_blocks_written)
		edid->blocks = bt1120->edid_blocks_written - edid->start_block;

	/* edid access by apb when read, i2c slave addr: 0x0 */
	rk628_i2c_update_bits(bt1120->rk628, GRF_SYSTEM_CON0,
			SW_ADAPTER_I2CSLADR_MASK |
			SW_EDID_MODE_MASK,
			SW_ADAPTER_I2CSLADR(0) |
			SW_EDID_MODE(1));

	for (i = 0; i < (edid->blocks * EDID_BLOCK_SIZE); i++) {
		rk628_i2c_read(bt1120->rk628, EDID_BASE + ((edid->start_block *
				EDID_BLOCK_SIZE) + i) * 4, &val);
		edid->edid[i] = val;
	}

	rk628_i2c_update_bits(bt1120->rk628, GRF_SYSTEM_CON0,
			SW_EDID_MODE_MASK,
			SW_EDID_MODE(0));

	return 0;
}

static int rk628_bt1120_s_edid(struct v4l2_subdev *sd,
				struct v4l2_subdev_edid *edid)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);
	u16 edid_len = edid->blocks * EDID_BLOCK_SIZE;
	u32 i, val;

	v4l2_dbg(1, debug, sd, "%s, pad %d, start block %d, blocks %d\n",
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

	rk628_hdmirx_hpd_ctrl(sd, false);

	if (edid->blocks == 0) {
		bt1120->edid_blocks_written = 0;
		return 0;
	}

	/* edid access by apb when write, i2c slave addr: 0x0 */
	rk628_i2c_update_bits(bt1120->rk628, GRF_SYSTEM_CON0,
			SW_ADAPTER_I2CSLADR_MASK |
			SW_EDID_MODE_MASK,
			SW_ADAPTER_I2CSLADR(0) |
			SW_EDID_MODE(1));

	for (i = 0; i < edid_len; i++)
		rk628_i2c_write(bt1120->rk628, EDID_BASE + i * 4, edid->edid[i]);

	/* read out for debug */
	if (debug >= 3) {
		pr_info("%s: Read EDID: ======\n", __func__);
		for (i = 0; i < edid_len; i++) {
			rk628_i2c_read(bt1120->rk628, EDID_BASE + i * 4, &val);
			pr_info("0x%02x ", val);
			if ((i + 1) % 8 == 0)
				pr_info("\n");
		}
		pr_info("%s: ======\n", __func__);
	}

	/* edid access by RX's i2c, i2c slave addr: 0x0 */
	rk628_i2c_update_bits(bt1120->rk628, GRF_SYSTEM_CON0,
			SW_ADAPTER_I2CSLADR_MASK |
			SW_EDID_MODE_MASK,
			SW_ADAPTER_I2CSLADR(0) |
			SW_EDID_MODE(0));
	bt1120->edid_blocks_written = edid->blocks;
	udelay(100);

	if (tx_5v_power_present(sd))
		rk628_hdmirx_hpd_ctrl(sd, true);

	return 0;
}

static int rk628_bt1120_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);
	const struct rk628_bt1120_mode *mode = bt1120->cur_mode;

	mutex_lock(&bt1120->confctl_mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&bt1120->confctl_mutex);

	return 0;
}

static int rk628_bt1120_querystd(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	*std = V4L2_STD_ATSC;

	return 0;
}

static void rk628_bt1120_get_module_inf(struct rk628_bt1120 *rk628_bt1120,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, RK628_BT1120_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, rk628_bt1120->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, rk628_bt1120->len_name, sizeof(inf->base.lens));
}

static long rk628_bt1120_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);
	long ret = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		rk628_bt1120_get_module_inf(bt1120, (struct rkmodule_inf *)arg);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long rk628_bt1120_compat_ioctl32(struct v4l2_subdev *sd,
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

		ret = rk628_bt1120_ioctl(sd, cmd, inf);
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

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int bt1120_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct rk628_bt1120 *bt1120 = to_bt1120(sd);
	struct v4l2_bt_timings *bt = &(bt1120->timings.bt);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct rk628_bt1120_mode *def_mode = &supported_modes[0];

	mutex_lock(&bt1120->confctl_mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_UYVY8_2X8;
	if (bt->interlaced == V4L2_DV_INTERLACED)
		try_fmt->field = V4L2_FIELD_INTERLACED;
	else
		try_fmt->field = V4L2_FIELD_NONE;
	mutex_unlock(&bt1120->confctl_mutex);

	return 0;
}

static const struct v4l2_subdev_internal_ops bt1120_subdev_internal_ops = {
	.open = bt1120_open,
};
#endif

static const struct v4l2_ctrl_ops rk628_bt1120_ctrl_ops = {
	.g_volatile_ctrl = rk628_bt1120_get_ctrl,
};

static const struct v4l2_subdev_core_ops rk628_bt1120_core_ops = {
	.interrupt_service_routine = rk628_bt1120_isr,
	.subscribe_event = rk628_bt1120_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
	.ioctl = rk628_bt1120_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = rk628_bt1120_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops rk628_bt1120_video_ops = {
	.g_input_status = rk628_bt1120_g_input_status,
	.s_dv_timings = rk628_bt1120_s_dv_timings,
	.g_dv_timings = rk628_bt1120_g_dv_timings,
	.query_dv_timings = rk628_bt1120_query_dv_timings,
	.s_stream = rk628_bt1120_s_stream,
	.g_frame_interval = rk628_bt1120_g_frame_interval,
	.querystd = rk628_bt1120_querystd,
};

static const struct v4l2_subdev_pad_ops rk628_bt1120_pad_ops = {
	.enum_mbus_code = rk628_bt1120_enum_mbus_code,
	.enum_frame_size = rk628_bt1120_enum_frame_sizes,
	.enum_frame_interval = rk628_bt1120_enum_frame_interval,
	.set_fmt = rk628_bt1120_set_fmt,
	.get_fmt = rk628_bt1120_get_fmt,
	.get_edid = rk628_bt1120_g_edid,
	.set_edid = rk628_bt1120_s_edid,
	.enum_dv_timings = rk628_bt1120_enum_dv_timings,
	.dv_timings_cap = rk628_bt1120_dv_timings_cap,
	.get_mbus_config = rk628_bt1120_g_mbus_config,
};

static const struct v4l2_subdev_ops rk628_bt1120_ops = {
	.core = &rk628_bt1120_core_ops,
	.video = &rk628_bt1120_video_ops,
	.pad = &rk628_bt1120_pad_ops,
};

static const struct v4l2_ctrl_config rk628_bt1120_ctrl_audio_sampling_rate = {
	.id = RK_V4L2_CID_AUDIO_SAMPLING_RATE,
	.name = "Audio sampling rate",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 768000,
	.step = 1,
	.def = 0,
	.flags = V4L2_CTRL_FLAG_READ_ONLY,
};

static const struct v4l2_ctrl_config rk628_bt1120_ctrl_audio_present = {
	.id = RK_V4L2_CID_AUDIO_PRESENT,
	.name = "Audio present",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 0,
	.flags = V4L2_CTRL_FLAG_READ_ONLY,
};

static irqreturn_t plugin_detect_irq(int irq, void *dev_id)
{
	struct rk628_bt1120 *bt1120 = dev_id;
	struct v4l2_subdev *sd = &bt1120->sd;

	/* control hpd after 50ms */
	schedule_delayed_work(&bt1120->delayed_work_enable_hotplug, HZ / 20);
	tx_5v_power_present(sd);

	return IRQ_HANDLED;
}

static int rk628_bt1120_probe_of(struct rk628_bt1120 *bt1120)
{
	struct device *dev = bt1120->dev;
	struct v4l2_fwnode_endpoint endpoint = { .bus_type = 0 };
	struct device_node *ep;
	int ret = -EINVAL;
	bool hdcp1x_enable = false, i2s_enable_default = false;
	bool scaler_en = false;

	bt1120->soc_24M = devm_clk_get(dev, "soc_24M");
	if (bt1120->soc_24M == ERR_PTR(-ENOENT))
		bt1120->soc_24M = NULL;
	if (IS_ERR(bt1120->soc_24M)) {
		ret = PTR_ERR(bt1120->soc_24M);
		dev_err(dev, "Unable to get soc_24M: %d\n", ret);
	}
	clk_prepare_enable(bt1120->soc_24M);

	bt1120->enable_gpio = devm_gpiod_get_optional(dev, "enable",
						     GPIOD_OUT_LOW);
	if (IS_ERR(bt1120->enable_gpio)) {
		ret = PTR_ERR(bt1120->enable_gpio);
		dev_err(dev, "failed to request enable GPIO: %d\n", ret);
		return ret;
	}

	bt1120->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(bt1120->reset_gpio)) {
		ret = PTR_ERR(bt1120->reset_gpio);
		dev_err(dev, "failed to request reset GPIO: %d\n", ret);
		return ret;
	}

	bt1120->power_gpio = devm_gpiod_get_optional(dev, "power", GPIOD_OUT_HIGH);
	if (IS_ERR(bt1120->power_gpio)) {
		dev_err(dev, "failed to get power gpio\n");
		ret = PTR_ERR(bt1120->power_gpio);
		return ret;
	}

	bt1120->plugin_det_gpio = devm_gpiod_get_optional(dev, "plugin-det",
						    GPIOD_IN);
	if (IS_ERR(bt1120->plugin_det_gpio)) {
		dev_err(dev, "failed to get hdmirx det gpio\n");
		ret = PTR_ERR(bt1120->plugin_det_gpio);
		return ret;
	}

	if (bt1120->enable_gpio) {
		gpiod_set_value(bt1120->enable_gpio, 1);
		usleep_range(10000, 11000);
	}
	gpiod_set_value(bt1120->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value(bt1120->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value(bt1120->reset_gpio, 0);
	usleep_range(10000, 11000);

	if (bt1120->power_gpio) {
		gpiod_set_value(bt1120->power_gpio, 1);
		usleep_range(500, 510);
	}

	if (of_property_read_bool(dev->of_node, "hdcp-enable"))
		hdcp1x_enable = true;

	if (of_property_read_bool(dev->of_node, "i2s-enable-default"))
		i2s_enable_default = true;

	if (of_property_read_bool(dev->of_node, "scaler-en"))
		scaler_en = true;

	if (of_property_read_bool(dev->of_node, "dual-edge"))
		bt1120->dual_edge = true;

	ep = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!ep) {
		dev_err(dev, "missing endpoint node\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_alloc_parse(of_fwnode_handle(ep), &endpoint);
	if (ret) {
		dev_err(dev, "failed to parse endpoint\n");
		of_node_put(ep);
		return ret;
	}

	bt1120->enable_hdcp = hdcp1x_enable;
	bt1120->i2s_enable_default = i2s_enable_default;
	bt1120->scaler_en = scaler_en;
	if (bt1120->scaler_en)
		bt1120->timings = dst_timing;
	bt1120->rxphy_pwron = false;
	bt1120->nosignal = true;
	bt1120->stream_state = 0;
	bt1120->avi_rcv_rdy = false;

	ret = 0;

	v4l2_fwnode_endpoint_free(&endpoint);

	return ret;
}

static int rk628_bt1120_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct rk628_bt1120 *bt1120;
	struct v4l2_subdev *sd;
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	char facing[2];
	int err;
	struct rk628 *rk628;
	unsigned long irq_flags;

	dev_info(dev, "RK628 I2C driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	if (!of_device_is_available(dev->of_node))
		return -ENODEV;

	bt1120 = devm_kzalloc(dev, sizeof(*bt1120), GFP_KERNEL);
	if (!bt1120)
		return -ENOMEM;

	bt1120->dev = dev;
	bt1120->i2c_client = client;
	rk628 = rk628_i2c_register(client);
	if (!rk628)
		return -ENOMEM;
	bt1120->rk628 = rk628;
	bt1120->cur_mode = &supported_modes[0];
	bt1120->hdmirx_irq = client->irq;
	sd = &bt1120->sd;
	sd->dev = dev;

	bt1120->hpd_output_inverted = of_property_read_bool(node,
			"hpd-output-inverted");
	err = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &bt1120->module_index);
	err |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &bt1120->module_facing);
	err |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &bt1120->module_name);
	err |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &bt1120->len_name);
	if (err) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	err = rk628_bt1120_probe_of(bt1120);
	if (err) {
		v4l2_err(sd, "rk628_bt1120_probe_of failed! err:%d\n", err);
		return err;
	}

	rk628_cru_initialize(rk628);

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	v4l2_i2c_subdev_init(sd, client, &rk628_bt1120_ops);
	sd->internal_ops = &bt1120_subdev_internal_ops;
#endif

	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;

	mutex_init(&bt1120->confctl_mutex);

	/* control handlers */
	v4l2_ctrl_handler_init(&bt1120->hdl, 2);
	bt1120->link_freq = v4l2_ctrl_new_int_menu(&bt1120->hdl, NULL,
			V4L2_CID_LINK_FREQ,
			ARRAY_SIZE(link_freq_menu_items) - 1,
			0, link_freq_menu_items);
	bt1120->pixel_rate = v4l2_ctrl_new_std(&bt1120->hdl, NULL,
			V4L2_CID_PIXEL_RATE, 0, RK628_CSI_PIXEL_RATE_HIGH, 1,
			RK628_CSI_PIXEL_RATE_HIGH);
	bt1120->detect_tx_5v_ctrl = v4l2_ctrl_new_std(&bt1120->hdl,
			&rk628_bt1120_ctrl_ops, V4L2_CID_DV_RX_POWER_PRESENT,
			0, 1, 0, 0);
	if (bt1120->detect_tx_5v_ctrl)
		bt1120->detect_tx_5v_ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	/* custom controls */
	bt1120->audio_sampling_rate_ctrl = v4l2_ctrl_new_custom(&bt1120->hdl,
			&rk628_bt1120_ctrl_audio_sampling_rate, NULL);
	bt1120->audio_present_ctrl = v4l2_ctrl_new_custom(&bt1120->hdl,
			&rk628_bt1120_ctrl_audio_present, NULL);

	sd->ctrl_handler = &bt1120->hdl;
	if (bt1120->hdl.error) {
		err = bt1120->hdl.error;
		v4l2_err(sd, "cfg v4l2 ctrls failed! err:%d\n", err);
		goto err_hdl;
	}

	if (rk628_bt1120_update_controls(sd)) {
		err = -ENODEV;
		v4l2_err(sd, "update v4l2 ctrls failed! err:%d\n", err);
		goto err_hdl;
	}

	bt1120->pad.flags = MEDIA_PAD_FL_SOURCE;
#if defined(CONFIG_MEDIA_CONTROLLER)
	bt1120->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	err = media_entity_pads_init(&sd->entity, 1, &bt1120->pad);
#endif

	if (err < 0) {
		v4l2_err(sd, "media entity init failed! err:%d\n", err);
		goto err_hdl;
	}

	bt1120->mbus_fmt_code = MEDIA_BUS_FMT_UYVY8_2X8;

	memset(facing, 0, sizeof(facing));
	if (strcmp(bt1120->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 bt1120->module_index, facing,
		 RK628_BT1120_NAME, dev_name(sd->dev));
	err = v4l2_async_register_subdev(sd);
	if (err < 0) {
		v4l2_err(sd, "v4l2 register subdev failed! err:%d\n", err);
		goto err_hdl;
	}

	INIT_DELAYED_WORK(&bt1120->delayed_work_enable_hotplug,
			rk628_bt1120_delayed_work_enable_hotplug);
	INIT_DELAYED_WORK(&bt1120->delayed_work_res_change,
			rk628_delayed_work_res_change);
	bt1120->audio_info = rk628_hdmirx_audioinfo_alloc(dev,
							  &bt1120->confctl_mutex,
							  rk628,
							  bt1120->i2s_enable_default);
	if (!bt1120->audio_info) {
		v4l2_err(sd, "request audio info fail\n");
		goto err_work_queues;
	}
	rk628_bt1120_initial_setup(sd);

	if (bt1120->hdmirx_irq) {
		irq_flags = irqd_get_trigger_type(irq_get_irq_data(bt1120->hdmirx_irq));
		v4l2_dbg(1, debug, sd, "cfg hdmirx irq, flags: %lu!\n", irq_flags);
		err = devm_request_threaded_irq(dev, bt1120->hdmirx_irq, NULL,
				rk628_bt1120_irq_handler, irq_flags |
				IRQF_ONESHOT, "rk628_bt1120", bt1120);
		if (err) {
			v4l2_err(sd, "request rk628-bt1120 irq failed! err:%d\n",
					err);
			goto err_work_queues;
		}
	} else {
		v4l2_dbg(1, debug, sd, "no irq, cfg poll!\n");
		INIT_WORK(&bt1120->work_i2c_poll,
			  rk628_bt1120_work_i2c_poll);
		timer_setup(&bt1120->timer, rk628_bt1120_irq_poll_timer, 0);
		bt1120->timer.expires = jiffies +
				       msecs_to_jiffies(POLL_INTERVAL_MS);
		add_timer(&bt1120->timer);
	}

	if (bt1120->plugin_det_gpio) {
		bt1120->plugin_irq = gpiod_to_irq(bt1120->plugin_det_gpio);
		if (bt1120->plugin_irq < 0) {
			dev_err(bt1120->dev, "failed to get plugin det irq\n");
			err = bt1120->plugin_irq;
			goto err_work_queues;
		}

		err = devm_request_threaded_irq(dev, bt1120->plugin_irq, NULL,
				plugin_detect_irq, IRQF_TRIGGER_FALLING |
				IRQF_TRIGGER_RISING | IRQF_ONESHOT, "rk628_bt1120", bt1120);
		if (err) {
			dev_err(bt1120->dev, "failed to register plugin det irq (%d)\n", err);
			goto err_work_queues;
		}
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
	if (!bt1120->hdmirx_irq)
		flush_work(&bt1120->work_i2c_poll);
	cancel_delayed_work(&bt1120->delayed_work_enable_hotplug);
	cancel_delayed_work(&bt1120->delayed_work_res_change);
	rk628_hdmirx_audio_destroy(bt1120->audio_info);
err_hdl:
	mutex_destroy(&bt1120->confctl_mutex);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&bt1120->hdl);
	return err;
}

static int rk628_bt1120_remove(struct i2c_client *client)
{
	struct rk628_bt1120 *bt1120 = i2c_get_clientdata(client);

	if (!bt1120->hdmirx_irq) {
		del_timer_sync(&bt1120->timer);
		flush_work(&bt1120->work_i2c_poll);
	}
	cancel_delayed_work_sync(&bt1120->delayed_work_enable_hotplug);
	cancel_delayed_work_sync(&bt1120->delayed_work_res_change);
	rk628_hdmirx_audio_cancel_work_audio(bt1120->audio_info, true);
	rk628_hdmirx_audio_cancel_work_rate_change(bt1120->audio_info, true);

	if (bt1120->rxphy_pwron)
		rk628_rxphy_power_off(bt1120->rk628);

	mutex_destroy(&bt1120->confctl_mutex);

	rk628_control_assert(bt1120->rk628, RGU_HDMIRX);
	rk628_control_assert(bt1120->rk628, RGU_HDMIRX_PON);
	rk628_control_assert(bt1120->rk628, RGU_DECODER);
	rk628_control_assert(bt1120->rk628, RGU_CLK_RX);
	rk628_control_assert(bt1120->rk628, RGU_VOP);
	rk628_control_assert(bt1120->rk628, RGU_BT1120DEC);

	return 0;
}

static const struct i2c_device_id rk628_bt1120_i2c_id[] = {
	{ "rk628-bt1120-v4l2", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, rk628_bt1120_i2c_id);

static const struct of_device_id rk628_bt1120_of_match[] = {
	{ .compatible = "rockchip,rk628-bt1120-v4l2" },
	{}
};
MODULE_DEVICE_TABLE(of, rk628_bt1120_of_match);

static struct i2c_driver rk628_bt1120_i2c_driver = {
	.driver = {
		.name = "rk628-bt1120-v4l2",
		.of_match_table = of_match_ptr(rk628_bt1120_of_match),
	},
	.id_table = rk628_bt1120_i2c_id,
	.probe	= rk628_bt1120_probe,
	.remove = rk628_bt1120_remove,
};

module_i2c_driver(rk628_bt1120_i2c_driver);

MODULE_DESCRIPTION("Rockchip RK628 HDMI to BT120 bridge I2C driver");
MODULE_AUTHOR("Shunqing Chen <csq@rock-chips.com>");
MODULE_LICENSE("GPL");

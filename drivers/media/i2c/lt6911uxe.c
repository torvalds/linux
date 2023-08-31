// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 *
 * lt6911uxe HDMI to MIPI CSI-2 bridge driver.
 *
 * Author: Jianwei Fan <jianwei.fan@rock-chips.com>
 *
 * V0.0X01.0X00 first version.
 * V0.0X01.0X01 support DPHY 4K60.
 * V0.0X01.0X02 support BGR888 format.
 * V0.0X01.0X03 add more timing support.
 * V0.0X01.0X04
 *  1.fix some errors.
 *  2.add dphy timing reg.
 *
 */
// #define DEBUG
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

#define LT6911UXE_LINK_FREQ_1250M	1250000000
#define LT6911UXE_LINK_FREQ_900M	900000000
#define LT6911UXE_LINK_FREQ_600M	600000000
#define LT6911UXE_LINK_FREQ_450M	450000000
#define LT6911UXE_LINK_FREQ_400M	400000000
#define LT6911UXE_LINK_FREQ_300M	300000000
#define LT6911UXE_LINK_FREQ_200M	200000000
#define LT6911UXE_LINK_FREQ_150M	150000000
#define LT6911UXE_LINK_FREQ_100M	100000000
#define LT6911UXE_PIXEL_RATE		800000000

#define LT6911UXE_CHIPID	0x0221
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

#define HS_HALF			0xe080
#define HFP_HALF_H		0xe081
#define HFP_HALF_L		0xe082

#define VS			0xe083
#define VFP_H			0xe097
#define VFP_L			0xe098

#define PCLK_H			0xe085
#define PCLK_M			0xe086
#define PCLK_L			0xe087

#define BYTE_PCLK_H		0xe092
#define BYTE_PCLK_M		0xe093
#define BYTE_PCLK_L		0xe094

#define AUDIO_FS_VALUE_H	0xe090
#define AUDIO_FS_VALUE_L	0xe091

#define LNAE_NUM		0xe095
#define BUS_FMT			0xe096

#define STREAM_CTL		0xe0b0
#define ENABLE_STREAM		0x01
#define DISABLE_STREAM		0x00

//mipi phy timing
#define CLK_ZERO_REG		0xeaa7
#define CLK_PRE_REG		0xeaa8
#define CLK_POST_REG		0xeaa9
#define HS_LPX_REG		0xeaa4
#define HS_PREPARE_REG		0xeaa5
#define HS_TRAIL		0xeaa6
#define HS_RQST_PRE_REG		0xea8a

//bit[2:0] mipi hs delay
#define MIPI_TX_PT0_TX0_DLY	0xe23a
#define MIPI_TX_PT0_TX1_DLY	0xe23b
#define MIPI_TX_PT0_TXC_DLY	0xe23c
#define MIPI_TX_PT0_TX2_DLY	0xe23d
#define MIPI_TX_PT0_TX3_DLY	0xe23e

#define MIPI_TX_PT1_TX0_DLY	0xe24a
#define MIPI_TX_PT1_TX1_DLY	0xe24b
#define MIPI_TX_PT1_TXC_DLY	0xe24c
#define MIPI_TX_PT1_TX2_DLY	0xe24d
#define MIPI_TX_PT1_TX3_DLY	0xe24e

#define MIPI_TIMING_MASK	0x7
//LP driver level
#define MIPI_TX_PT0_LPTX	0xe234
#define MIPI_TX_PT1_LPTX	0xe244

// #define LT6911UXE_OUT_RGB
#ifdef LT6911UXE_OUT_RGB
#define LT6911UXE_MEDIA_BUS_FMT		MEDIA_BUS_FMT_BGR888_1X24
#else
#define LT6911UXE_MEDIA_BUS_FMT		MEDIA_BUS_FMT_UYVY8_2X8
#endif

#define LT6911UXE_NAME			"LT6911UXE"

#ifdef LT6911UXE_OUT_RGB
static const s64 link_freq_menu_items[] = {
	LT6911UXE_LINK_FREQ_1250M,
	LT6911UXE_LINK_FREQ_900M,
	LT6911UXE_LINK_FREQ_600M,
	LT6911UXE_LINK_FREQ_450M,
	LT6911UXE_LINK_FREQ_300M,
	LT6911UXE_LINK_FREQ_150M,
};
#else
static const s64 link_freq_menu_items[] = {
	LT6911UXE_LINK_FREQ_1250M,
	LT6911UXE_LINK_FREQ_600M,
	LT6911UXE_LINK_FREQ_400M,
	LT6911UXE_LINK_FREQ_300M,
	LT6911UXE_LINK_FREQ_200M,
	LT6911UXE_LINK_FREQ_100M,
};
#endif

struct lt6911uxe {
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
	const struct lt6911uxe_mode *cur_mode;
	const struct lt6911uxe_mode *support_modes;
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
	int lane_in_use;
};

static const struct v4l2_dv_timings_cap lt6911uxe_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	.reserved = { 0 },
	V4L2_INIT_BT_TIMINGS(1, 10000, 1, 10000, 0, 800000000,
			V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
			V4L2_DV_BT_STD_GTF | V4L2_DV_BT_STD_CVT,
			V4L2_DV_BT_CAP_PROGRESSIVE | V4L2_DV_BT_CAP_INTERLACED |
			V4L2_DV_BT_CAP_REDUCED_BLANKING |
			V4L2_DV_BT_CAP_CUSTOM)
};

struct lt6911uxe_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	u32 mipi_freq_idx;
	u32 interlace;
};

static struct rkmodule_csi_dphy_param rk3588_dcphy_param = {
	.vendor = PHY_VENDOR_SAMSUNG,
	.lp_vol_ref = 3,
	.lp_hys_sw = {3, 0, 3, 0},
	.lp_escclk_pol_sel = {1, 1, 0, 0},
	.skew_data_cal_clk = {0, 13, 0, 13},
	.clk_hs_term_sel = 2,
	.data_hs_term_sel = {2, 2, 2, 2},
	.reserved = {0},
};

static const struct lt6911uxe_mode supported_modes_dphy[] = {
	{
		.width = 5120,
		.height = 2160,
		.max_fps = {
			.numerator = 10000,
			.denominator = 480000,
		},
		.hts_def = 5500,
		.vts_def = 2250,
		.mipi_freq_idx = 0,
		.interlace = 0,
	}, {
		.width = 4096,
		.height = 2160,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 4400,
		.vts_def = 2250,
		.mipi_freq_idx = 0,
		.interlace = 0,
	}, {
		.width = 4096,
		.height = 2160,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.hts_def = 4400,
		.vts_def = 2250,
		.mipi_freq_idx = 1,
		.interlace = 0,
	}, {
		.width = 3840,
		.height = 2160,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 4400,
		.vts_def = 2250,
		.mipi_freq_idx = 0,
		.interlace = 0,
	}, {
		.width = 3840,
		.height = 2160,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.hts_def = 4400,
		.vts_def = 2250,
		.mipi_freq_idx = 1,
		.interlace = 0,
	}, {
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 2200,
		.vts_def = 1125,
		.mipi_freq_idx = 3,
		.interlace = 0,
	}, {
		.width = 1920,
		.height = 1200,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 2592,
		.vts_def = 1245,
		.mipi_freq_idx = 3,
		.interlace = 0,
	}, {
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.hts_def = 2200,
		.vts_def = 1125,
		.mipi_freq_idx = 4,
		.interlace = 0,
	}, {
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 2200,
		.vts_def = 1125,
		.mipi_freq_idx = 4,
		.interlace = 1,
	}, {
		.width = 1680,
		.height = 1050,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 2240,
		.vts_def = 1089,
		.mipi_freq_idx = 3,
		.interlace = 0,
	}, {
		.width = 1600,
		.height = 1200,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 2160,
		.vts_def = 1250,
		.mipi_freq_idx = 3,
		.interlace = 0,
	}, {
		.width = 1600,
		.height = 900,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 1800,
		.vts_def = 1000,
		.mipi_freq_idx = 3,
		.interlace = 0,
	}, {
		.width = 1440,
		.height = 900,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 1904,
		.vts_def = 934,
		.mipi_freq_idx = 3,
		.interlace = 0,
	}, {
		.width = 1440,
		.height = 240,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 1716,
		.vts_def = 262,
		.mipi_freq_idx = 5,
		.interlace = 0,
	}, {
		.width = 1360,
		.height = 768,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 1792,
		.vts_def = 795,
		.mipi_freq_idx = 4,
		.interlace = 0,
	}, {
		.width = 1280,
		.height = 1024,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 1688,
		.vts_def = 1066,
		.mipi_freq_idx = 3,
		.interlace = 0,
	}, {
		.width = 1280,
		.height = 960,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 1712,
		.vts_def = 994,
		.mipi_freq_idx = 3,
		.interlace = 0,
	}, {
		.width = 1280,
		.height = 800,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 1680,
		.vts_def = 828,
		.mipi_freq_idx = 4,
		.interlace = 0,
	}, {
		.width = 1280,
		.height = 768,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 1664,
		.vts_def = 798,
		.mipi_freq_idx = 4,
		.interlace = 0,
	}, {
		.width = 1280,
		.height = 720,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 1650,
		.vts_def = 750,
		.mipi_freq_idx = 4,
		.interlace = 0,
	}, {
		.width = 1152,
		.height = 864,
		.max_fps = {
			.numerator = 10000,
			.denominator = 750000,
		},
		.hts_def = 1600,
		.vts_def = 900,
		.mipi_freq_idx = 4,
		.interlace = 0,
	}, {
		.width = 1024,
		.height = 768,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 1344,
		.vts_def = 806,
		.mipi_freq_idx = 4,
		.interlace = 0,
	}, {
		.width = 800,
		.height = 600,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 1056,
		.vts_def = 628,
		.mipi_freq_idx = 5,
		.interlace = 0,
	}, {
		.width = 720,
		.height = 576,
		.max_fps = {
			.numerator = 10000,
			.denominator = 500000,
		},
		.hts_def = 864,
		.vts_def = 625,
		.mipi_freq_idx = 5,
		.interlace = 0,
	}, {
		.width = 720,
		.height = 480,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 858,
		.vts_def = 525,
		.mipi_freq_idx = 5,
		.interlace = 0,
	}, {
		.width = 720,
		.height = 400,
		.max_fps = {
			.numerator = 10000,
			.denominator = 850000,
		},
		.hts_def = 936,
		.vts_def = 446,
		.mipi_freq_idx = 5,
		.interlace = 0,
	}, {
		.width = 720,
		.height = 240,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.mipi_freq_idx = 5,
		.interlace = 0,
	}, {
		.width = 640,
		.height = 480,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 800,
		.vts_def = 525,
		.mipi_freq_idx = 5,
		.interlace = 0,
	},
};

static void lt6911uxe_format_change(struct v4l2_subdev *sd);
static int lt6911uxe_s_ctrl_detect_tx_5v(struct v4l2_subdev *sd);
static int lt6911uxe_s_dv_timings(struct v4l2_subdev *sd,
				struct v4l2_dv_timings *timings);

static inline struct lt6911uxe *to_lt6911uxe(struct v4l2_subdev *sd)
{
	return container_of(sd, struct lt6911uxe, sd);
}

static void i2c_rd(struct v4l2_subdev *sd, u16 reg, u8 *values, u32 n)
{
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);
	struct i2c_client *client = lt6911uxe->i2c_client;
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
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);
	struct i2c_client *client = lt6911uxe->i2c_client;
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

static __maybe_unused void i2c_wr8_and_or(struct v4l2_subdev *sd, u16 reg, u32 mask,
			   u8 val)
{
	u8 val_p;

	val_p = i2c_rd8(sd, reg);
	i2c_wr8(sd, reg, (val_p & mask) | val);
}

static void lt6911uxe_i2c_enable(struct v4l2_subdev *sd)
{
	i2c_wr8(sd, I2C_EN_REG, I2C_ENABLE);
}

static void lt6911uxe_i2c_disable(struct v4l2_subdev *sd)
{
	i2c_wr8(sd, I2C_EN_REG, I2C_DISABLE);
}

static inline bool tx_5v_power_present(struct v4l2_subdev *sd)
{
	bool ret;
	int val, i, cnt;
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);

	/* if not use plugin det gpio */
	if (!lt6911uxe->plugin_det_gpio)
		return true;

	cnt = 0;
	for (i = 0; i < 5; i++) {
		val = gpiod_get_value(lt6911uxe->plugin_det_gpio);
		if (val > 0)
			cnt++;
		usleep_range(500, 600);
	}

	ret = (cnt >= 4) ? true : false;
	v4l2_dbg(1, debug, sd, "%s: %d\n", __func__, ret);

	return ret;
}

static inline bool no_signal(struct v4l2_subdev *sd)
{
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);

	v4l2_dbg(1, debug, sd, "%s no signal:%d\n", __func__,
			lt6911uxe->nosignal);

	return lt6911uxe->nosignal;
}

static inline bool audio_present(struct v4l2_subdev *sd)
{
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);

	return lt6911uxe->is_audio_present;
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

static bool lt6911uxe_rcv_supported_res(struct v4l2_subdev *sd, u32 width,
		u32 height)
{
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);
	u32 i;

	for (i = 0; i < lt6911uxe->cfg_num; i++) {
		if ((lt6911uxe->support_modes[i].width == width) &&
		    (lt6911uxe->support_modes[i].height == height)) {
			break;
		}
	}

	if (i == lt6911uxe->cfg_num) {
		v4l2_err(sd, "%s do not support res wxh: %dx%d\n", __func__,
				width, height);
		return false;
	} else {
		return true;
	}
}

static int lt6911uxe_get_detected_timings(struct v4l2_subdev *sd,
				     struct v4l2_dv_timings *timings)
{
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);
	struct v4l2_bt_timings *bt = &timings->bt;
	u32 hact, vact, htotal, vtotal, hs, vs, hbp, vbp, hfp, vfp;
	u32 pixel_clock, fps, halt_pix_clk;
	u8 clk_h, clk_m, clk_l;
	u8 val_h, val_l;
	u32 byte_clk, mipi_clk, mipi_data_rate;

	memset(timings, 0, sizeof(struct v4l2_dv_timings));
	lt6911uxe_i2c_enable(sd);

	clk_h = i2c_rd8(sd, PCLK_H);
	clk_m = i2c_rd8(sd, PCLK_M);
	clk_l = i2c_rd8(sd, PCLK_L);
	halt_pix_clk = ((clk_h << 16) | (clk_m << 8) | clk_l);
	pixel_clock = halt_pix_clk * 1000 * 2;

	clk_h = i2c_rd8(sd, BYTE_PCLK_H);
	clk_m = i2c_rd8(sd, BYTE_PCLK_M);
	clk_l = i2c_rd8(sd, BYTE_PCLK_L);
	byte_clk = ((clk_h << 16) | (clk_m << 8) | clk_l) * 1000;
	mipi_clk = byte_clk * 4;
	mipi_data_rate = byte_clk * 8;

	val_h = i2c_rd8(sd, HTOTAL_H);
	val_l = i2c_rd8(sd, HTOTAL_L);
	htotal = ((val_h << 8) | val_l) * 2;

	val_h = i2c_rd8(sd, VTOTAL_H);
	val_l = i2c_rd8(sd, VTOTAL_L);
	vtotal = (val_h << 8) | val_l;

	val_h = i2c_rd8(sd, HACT_H);
	val_l = i2c_rd8(sd, HACT_L);
	hact = ((val_h << 8) | val_l) * 2;

	val_h = i2c_rd8(sd, VACT_H);
	val_l = i2c_rd8(sd, VACT_L);
	vact = (val_h << 8) | val_l;

	hs = i2c_rd8(sd, HS_HALF) * 2;

	val_h = i2c_rd8(sd, HFP_HALF_H);
	val_l = i2c_rd8(sd, HFP_HALF_L);
	hfp = ((val_h << 8) | val_l) * 2;

	hbp = htotal - hact - hs - hfp;

	vs = i2c_rd8(sd, VS);
	val_h = i2c_rd8(sd, VFP_H);
	val_l = i2c_rd8(sd, VFP_L);
	vfp = (val_h << 8) | val_l;

	vbp = vtotal - vact - vs - vfp;
	lt6911uxe_i2c_disable(sd);

	lt6911uxe->nosignal = false;
	lt6911uxe->is_audio_present = true;
	timings->type = V4L2_DV_BT_656_1120;
	bt->interlaced = V4L2_DV_PROGRESSIVE;
	bt->width = hact;
	bt->height = vact;
	bt->vsync = vs;
	bt->hsync = hs;
	bt->hfrontporch = hfp;
	bt->vfrontporch = vfp;
	bt->hbackporch = hbp;
	bt->vbackporch = vbp;
	bt->pixelclock = pixel_clock;
	fps = pixel_clock / (htotal * vtotal);

	/* for interlaced res 1080i 576i 480i*/
	if ((hact == 1920 && vact == 540) || (hact == 1440 && vact == 288)
			|| (hact == 1440 && vact == 240)) {
		bt->interlaced = V4L2_DV_INTERLACED;
		bt->height *= 2;
		bt->il_vsync = bt->vsync + 1;
	} else {
		bt->interlaced = V4L2_DV_PROGRESSIVE;
	}

	if (!lt6911uxe_rcv_supported_res(sd, hact, bt->height)) {
		lt6911uxe->nosignal = true;
		v4l2_err(sd, "%s: rcv err res, return no signal!\n", __func__);
		return -EINVAL;
	}

	v4l2_info(sd, "act:%dx%d, total:%dx%d, pixclk:%d, fps:%d\n",
			hact, vact, htotal, vtotal, pixel_clock, fps);
	v4l2_info(sd, "byte_clk:%u, mipi_clk:%u, mipi_data_rate:%u\n",
			byte_clk, mipi_clk, mipi_data_rate);
	v4l2_info(sd, "hfp:%d, hs:%d, hbp:%d, vfp:%d, vs:%d, vbp:%d, inerlaced:%d\n",
			bt->hfrontporch, bt->hsync, bt->hbackporch, bt->vfrontporch,
			bt->vsync, bt->vbackporch, bt->interlaced);

	return 0;
}

static void lt6911uxe_delayed_work_hotplug(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct lt6911uxe *lt6911uxe = container_of(dwork,
			struct lt6911uxe, delayed_work_hotplug);
	struct v4l2_subdev *sd = &lt6911uxe->sd;

	lt6911uxe_s_ctrl_detect_tx_5v(sd);
}

static void lt6911uxe_delayed_work_res_change(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct lt6911uxe *lt6911uxe = container_of(dwork,
			struct lt6911uxe, delayed_work_res_change);
	struct v4l2_subdev *sd = &lt6911uxe->sd;

	lt6911uxe_format_change(sd);
}

static int lt6911uxe_s_ctrl_detect_tx_5v(struct v4l2_subdev *sd)
{
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);

	return v4l2_ctrl_s_ctrl(lt6911uxe->detect_tx_5v_ctrl,
			tx_5v_power_present(sd));
}

static int lt6911uxe_s_ctrl_audio_sampling_rate(struct v4l2_subdev *sd)
{
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);

	return v4l2_ctrl_s_ctrl(lt6911uxe->audio_sampling_rate_ctrl,
			get_audio_sampling_rate(sd));
}

static int lt6911uxe_s_ctrl_audio_present(struct v4l2_subdev *sd)
{
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);

	return v4l2_ctrl_s_ctrl(lt6911uxe->audio_present_ctrl,
			audio_present(sd));
}

static int lt6911uxe_update_controls(struct v4l2_subdev *sd)
{
	int ret = 0;

	ret |= lt6911uxe_s_ctrl_detect_tx_5v(sd);
	ret |= lt6911uxe_s_ctrl_audio_sampling_rate(sd);
	ret |= lt6911uxe_s_ctrl_audio_present(sd);

	return ret;
}

static void lt6911uxe_config_dphy_timing(struct v4l2_subdev *sd)
{
	u8 val;

	val = i2c_rd8(sd, CLK_ZERO_REG);
	i2c_wr8(sd, CLK_ZERO_REG, val);

	val = i2c_rd8(sd, HS_PREPARE_REG);
	i2c_wr8(sd, HS_PREPARE_REG, val);

	val = i2c_rd8(sd, HS_TRAIL);
	i2c_wr8(sd, HS_TRAIL, val);
	v4l2_info(sd, "%s: dphy timing: hs trail = %x\n", __func__, val);

	val = i2c_rd8(sd, MIPI_TX_PT0_TX0_DLY);
	i2c_wr8_and_or(sd, MIPI_TX_PT0_TX0_DLY, ~MIPI_TIMING_MASK, val);
	v4l2_info(sd, "%s: dphy timing: port0 tx0 delay = %x\n", __func__, val);

	val = i2c_rd8(sd, MIPI_TX_PT0_LPTX);
	i2c_wr8(sd, MIPI_TX_PT0_LPTX, val);
	v4l2_info(sd, "%s: dphy timing: port0 lptx = %x\n", __func__, val);

	v4l2_info(sd, "%s: dphy timing config done.\n", __func__);
}

static inline void enable_stream(struct v4l2_subdev *sd, bool enable)
{
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);

	lt6911uxe_i2c_enable(sd);
	if (enable) {
		lt6911uxe_config_dphy_timing(sd);
		usleep_range(5000, 6000);
		i2c_wr8(&lt6911uxe->sd, STREAM_CTL, ENABLE_STREAM);
	} else {
		i2c_wr8(&lt6911uxe->sd, STREAM_CTL, DISABLE_STREAM);
	}
	lt6911uxe_i2c_disable(sd);
	msleep(20);

	v4l2_dbg(2, debug, sd, "%s: %sable\n",
			__func__, enable ? "en" : "dis");
}

static void lt6911uxe_format_change(struct v4l2_subdev *sd)
{
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);
	struct v4l2_dv_timings timings;
	const struct v4l2_event lt6911uxe_ev_fmt = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
	};

	if (lt6911uxe_get_detected_timings(sd, &timings)) {
		enable_stream(sd, false);
		v4l2_dbg(1, debug, sd, "%s: No signal\n", __func__);
	}

	if (!v4l2_match_dv_timings(&lt6911uxe->timings, &timings, 0, false)) {
		enable_stream(sd, false);
		/* automatically set timing rather than set by user */
		lt6911uxe_s_dv_timings(sd, &timings);
		v4l2_print_dv_timings(sd->name,
				"Format_change: New format: ",
				&timings, false);
		if (sd->devnode && !lt6911uxe->i2c_client->irq)
			v4l2_subdev_notify_event(sd, &lt6911uxe_ev_fmt);
	}
	if (sd->devnode && lt6911uxe->i2c_client->irq)
		v4l2_subdev_notify_event(sd, &lt6911uxe_ev_fmt);
}

static int lt6911uxe_isr(struct v4l2_subdev *sd, u32 status, bool *handled)
{
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);

	schedule_delayed_work(&lt6911uxe->delayed_work_res_change, HZ / 20);
	*handled = true;

	return 0;
}

static irqreturn_t lt6911uxe_res_change_irq_handler(int irq, void *dev_id)
{
	struct lt6911uxe *lt6911uxe = dev_id;
	bool handled;

	lt6911uxe_isr(&lt6911uxe->sd, 0, &handled);

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

static irqreturn_t plugin_detect_irq_handler(int irq, void *dev_id)
{
	struct lt6911uxe *lt6911uxe = dev_id;

	/* control hpd output level after 25ms */
	schedule_delayed_work(&lt6911uxe->delayed_work_hotplug,
			HZ / 40);

	return IRQ_HANDLED;
}

static void lt6911uxe_irq_poll_timer(struct timer_list *t)
{
	struct lt6911uxe *lt6911uxe = from_timer(lt6911uxe, t, timer);

	schedule_work(&lt6911uxe->work_i2c_poll);
	mod_timer(&lt6911uxe->timer, jiffies + msecs_to_jiffies(POLL_INTERVAL_MS));
}

static void lt6911uxe_work_i2c_poll(struct work_struct *work)
{
	struct lt6911uxe *lt6911uxe = container_of(work,
			struct lt6911uxe, work_i2c_poll);
	struct v4l2_subdev *sd = &lt6911uxe->sd;

	lt6911uxe_format_change(sd);
}

static int lt6911uxe_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
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

static int lt6911uxe_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	*status = 0;
	*status |= no_signal(sd) ? V4L2_IN_ST_NO_SIGNAL : 0;

	v4l2_dbg(1, debug, sd, "%s: status = 0x%x\n", __func__, *status);

	return 0;
}

static int lt6911uxe_s_dv_timings(struct v4l2_subdev *sd,
				 struct v4l2_dv_timings *timings)
{
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);

	if (!timings)
		return -EINVAL;

	if (debug)
		v4l2_print_dv_timings(sd->name, "s_dv_timings: ",
				timings, false);

	if (v4l2_match_dv_timings(&lt6911uxe->timings, timings, 0, false)) {
		v4l2_dbg(1, debug, sd, "%s: no change\n", __func__);
		return 0;
	}

	if (!v4l2_valid_dv_timings(timings,
				&lt6911uxe_timings_cap, NULL, NULL)) {
		v4l2_dbg(1, debug, sd, "%s: timings out of range\n", __func__);
		return -ERANGE;
	}

	lt6911uxe->timings = *timings;

	enable_stream(sd, false);

	return 0;
}

static int lt6911uxe_g_dv_timings(struct v4l2_subdev *sd,
				struct v4l2_dv_timings *timings)
{
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);

	*timings = lt6911uxe->timings;

	return 0;
}

static int lt6911uxe_enum_dv_timings(struct v4l2_subdev *sd,
				struct v4l2_enum_dv_timings *timings)
{
	if (timings->pad != 0)
		return -EINVAL;

	return v4l2_enum_dv_timings_cap(timings,
			&lt6911uxe_timings_cap, NULL, NULL);
}

static int lt6911uxe_query_dv_timings(struct v4l2_subdev *sd,
				struct v4l2_dv_timings *timings)
{
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);

	*timings = lt6911uxe->timings;
	if (debug)
		v4l2_print_dv_timings(sd->name,
				"query_dv_timings: ", timings, false);

	if (!v4l2_valid_dv_timings(timings, &lt6911uxe_timings_cap, NULL,
				NULL)) {
		v4l2_dbg(1, debug, sd, "%s: timings out of range\n",
				__func__);

		return -ERANGE;
	}

	return 0;
}

static int lt6911uxe_dv_timings_cap(struct v4l2_subdev *sd,
				struct v4l2_dv_timings_cap *cap)
{
	if (cap->pad != 0)
		return -EINVAL;

	*cap = lt6911uxe_timings_cap;

	return 0;
}

static int lt6911uxe_g_mbus_config(struct v4l2_subdev *sd,
			unsigned int pad, struct v4l2_mbus_config *cfg)
{
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);
	u32 lane_num = lt6911uxe->bus_cfg.bus.mipi_csi2.num_data_lanes;
	u32 val = 0;

	val = 1 << (lane_num - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	cfg->type = lt6911uxe->bus_cfg.bus_type;
	cfg->flags = val;

	return 0;
}

static int lt6911uxe_s_stream(struct v4l2_subdev *sd, int on)
{
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);
	struct i2c_client *client = lt6911uxe->i2c_client;

	dev_info(&client->dev, "%s: on: %d, %dx%d%s%d\n", __func__, on,
				lt6911uxe->cur_mode->width,
				lt6911uxe->cur_mode->height,
				lt6911uxe->cur_mode->interlace ? "I" : "P",
		DIV_ROUND_CLOSEST(lt6911uxe->cur_mode->max_fps.denominator,
				  lt6911uxe->cur_mode->max_fps.numerator));
	enable_stream(sd, on);

	return 0;
}

static int lt6911uxe_enum_mbus_code(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_mbus_code_enum *code)
{
	switch (code->index) {
	case 0:
		code->code = LT6911UXE_MEDIA_BUS_FMT;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int lt6911uxe_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);

	if (fse->index >= lt6911uxe->cfg_num)
		return -EINVAL;

	if (fse->code != LT6911UXE_MEDIA_BUS_FMT)
		return -EINVAL;

	fse->min_width  = lt6911uxe->support_modes[fse->index].width;
	fse->max_width  = lt6911uxe->support_modes[fse->index].width;
	fse->max_height = lt6911uxe->support_modes[fse->index].height;
	fse->min_height = lt6911uxe->support_modes[fse->index].height;

	return 0;
}

static int lt6911uxe_enum_frame_interval(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_frame_interval_enum *fie)
{
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);

	if (fie->index >= lt6911uxe->cfg_num)
		return -EINVAL;

	fie->code = LT6911UXE_MEDIA_BUS_FMT;

	fie->width = lt6911uxe->support_modes[fie->index].width;
	fie->height = lt6911uxe->support_modes[fie->index].height;
	fie->interval = lt6911uxe->support_modes[fie->index].max_fps;

	return 0;
}

static int lt6911uxe_get_reso_dist(const struct lt6911uxe_mode *mode,
				struct v4l2_dv_timings *timings)
{
	struct v4l2_bt_timings *bt = &timings->bt;
	u32 cur_fps, dist_fps;

	cur_fps = fps_calc(bt);
	dist_fps = DIV_ROUND_CLOSEST(mode->max_fps.denominator, mode->max_fps.numerator);

	return abs(mode->width - bt->width) +
		abs(mode->height - bt->height) + abs(dist_fps - cur_fps);
}

static const struct lt6911uxe_mode *
lt6911uxe_find_best_fit(struct lt6911uxe *lt6911uxe)
{
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < lt6911uxe->cfg_num; i++) {
		if (lt6911uxe->support_modes[i].interlace == lt6911uxe->timings.bt.interlaced) {
			dist = lt6911uxe_get_reso_dist(&lt6911uxe->support_modes[i],
							&lt6911uxe->timings);
			if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
				cur_best_fit_dist = dist;
				cur_best_fit = i;
			}
		}
	}
	dev_info(&lt6911uxe->i2c_client->dev,
		"find current mode: support_mode[%d], %dx%d%s%dfps\n",
		cur_best_fit, lt6911uxe->support_modes[cur_best_fit].width,
		lt6911uxe->support_modes[cur_best_fit].height,
		lt6911uxe->support_modes[cur_best_fit].interlace ? "I" : "P",
		DIV_ROUND_CLOSEST(lt6911uxe->support_modes[cur_best_fit].max_fps.denominator,
		lt6911uxe->support_modes[cur_best_fit].max_fps.numerator));

	return &lt6911uxe->support_modes[cur_best_fit];
}

static int lt6911uxe_get_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_format *format)
{
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);
	const struct lt6911uxe_mode *mode;

	mutex_lock(&lt6911uxe->confctl_mutex);
	format->format.code = lt6911uxe->mbus_fmt_code;
	format->format.width = lt6911uxe->timings.bt.width;
	format->format.height = lt6911uxe->timings.bt.height;
	format->format.field =
		lt6911uxe->timings.bt.interlaced ?
		V4L2_FIELD_INTERLACED : V4L2_FIELD_NONE;
	format->format.colorspace = V4L2_COLORSPACE_SRGB;
	mutex_unlock(&lt6911uxe->confctl_mutex);

	mode = lt6911uxe_find_best_fit(lt6911uxe);
	lt6911uxe->cur_mode = mode;

	__v4l2_ctrl_s_ctrl_int64(lt6911uxe->pixel_rate,
				LT6911UXE_PIXEL_RATE);
	__v4l2_ctrl_s_ctrl(lt6911uxe->link_freq,
				mode->mipi_freq_idx);

	v4l2_dbg(1, debug, sd, "%s: mode->mipi_freq_idx(%d)", __func__, mode->mipi_freq_idx);

	v4l2_dbg(1, debug, sd, "%s: fmt code:%d, w:%d, h:%d, field code:%d\n",
			__func__, format->format.code, format->format.width,
			format->format.height, format->format.field);

	return 0;
}

static int lt6911uxe_set_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_format *format)
{
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);
	const struct lt6911uxe_mode *mode;

	/* is overwritten by get_fmt */
	u32 code = format->format.code;
	int ret = lt6911uxe_get_fmt(sd, cfg, format);

	format->format.code = code;

	if (ret)
		return ret;

	switch (code) {
	case LT6911UXE_MEDIA_BUS_FMT:
		break;

	default:
		return -EINVAL;
	}

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	lt6911uxe->mbus_fmt_code = format->format.code;
	mode = lt6911uxe_find_best_fit(lt6911uxe);
	lt6911uxe->cur_mode = mode;

	enable_stream(sd, false);

	return 0;
}

static int lt6911uxe_g_frame_interval(struct v4l2_subdev *sd,
			struct v4l2_subdev_frame_interval *fi)
{
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);
	const struct lt6911uxe_mode *mode = lt6911uxe->cur_mode;

	mutex_lock(&lt6911uxe->confctl_mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&lt6911uxe->confctl_mutex);

	return 0;
}

static void lt6911uxe_get_module_inf(struct lt6911uxe *lt6911uxe,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, LT6911UXE_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, lt6911uxe->module_name, sizeof(inf->base.module));
	strscpy(inf->base.lens, lt6911uxe->len_name, sizeof(inf->base.lens));
}

static long lt6911uxe_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);
	long ret = 0;
	struct rkmodule_csi_dphy_param *dphy_param;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		lt6911uxe_get_module_inf(lt6911uxe, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDMI_MODE:
		*(int *)arg = RKMODULE_HDMIIN_MODE;
		break;
	case RKMODULE_SET_CSI_DPHY_PARAM:
		dphy_param = (struct rkmodule_csi_dphy_param *)arg;
		if (dphy_param->vendor == PHY_VENDOR_SAMSUNG)
			rk3588_dcphy_param = *dphy_param;
		dev_dbg(&lt6911uxe->i2c_client->dev,
			"sensor set dphy param\n");
		break;
	case RKMODULE_GET_CSI_DPHY_PARAM:
		dphy_param = (struct rkmodule_csi_dphy_param *)arg;
		*dphy_param = rk3588_dcphy_param;
		dev_dbg(&lt6911uxe->i2c_client->dev,
			"sensor get dphy param\n");
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

static int lt6911uxe_s_power(struct v4l2_subdev *sd, int on)
{
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);
	int ret = 0;

	mutex_lock(&lt6911uxe->confctl_mutex);

	if (lt6911uxe->power_on == !!on)
		goto unlock_and_return;

	if (on)
		lt6911uxe->power_on = true;
	else
		lt6911uxe->power_on = false;

unlock_and_return:
	mutex_unlock(&lt6911uxe->confctl_mutex);

	return ret;
}

#ifdef CONFIG_COMPAT
static long lt6911uxe_compat_ioctl32(struct v4l2_subdev *sd,
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

		ret = lt6911uxe_ioctl(sd, cmd, inf);
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

		ret = lt6911uxe_ioctl(sd, cmd, seq);
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
			ret = lt6911uxe_ioctl(sd, cmd, dphy_param);
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

		ret = lt6911uxe_ioctl(sd, cmd, dphy_param);
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
static int lt6911uxe_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct lt6911uxe_mode *def_mode = &lt6911uxe->support_modes[0];

	mutex_lock(&lt6911uxe->confctl_mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = LT6911UXE_MEDIA_BUS_FMT;
	try_fmt->field = V4L2_FIELD_NONE;
	mutex_unlock(&lt6911uxe->confctl_mutex);

	return 0;
}
#endif

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops lt6911uxe_internal_ops = {
	.open = lt6911uxe_open,
};
#endif

static const struct v4l2_subdev_core_ops lt6911uxe_core_ops = {
	.s_power = lt6911uxe_s_power,
	.interrupt_service_routine = lt6911uxe_isr,
	.subscribe_event = lt6911uxe_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
	.ioctl = lt6911uxe_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = lt6911uxe_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops lt6911uxe_video_ops = {
	.g_input_status = lt6911uxe_g_input_status,
	.s_dv_timings = lt6911uxe_s_dv_timings,
	.g_dv_timings = lt6911uxe_g_dv_timings,
	.query_dv_timings = lt6911uxe_query_dv_timings,
	.s_stream = lt6911uxe_s_stream,
	.g_frame_interval = lt6911uxe_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops lt6911uxe_pad_ops = {
	.enum_mbus_code = lt6911uxe_enum_mbus_code,
	.enum_frame_size = lt6911uxe_enum_frame_sizes,
	.enum_frame_interval = lt6911uxe_enum_frame_interval,
	.set_fmt = lt6911uxe_set_fmt,
	.get_fmt = lt6911uxe_get_fmt,
	.enum_dv_timings = lt6911uxe_enum_dv_timings,
	.dv_timings_cap = lt6911uxe_dv_timings_cap,
	.get_mbus_config = lt6911uxe_g_mbus_config,
};

static const struct v4l2_subdev_ops lt6911uxe_ops = {
	.core = &lt6911uxe_core_ops,
	.video = &lt6911uxe_video_ops,
	.pad = &lt6911uxe_pad_ops,
};

static const struct v4l2_ctrl_config lt6911uxe_ctrl_audio_sampling_rate = {
	.id = RK_V4L2_CID_AUDIO_SAMPLING_RATE,
	.name = "Audio sampling rate",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 768000,
	.step = 1,
	.def = 0,
	.flags = V4L2_CTRL_FLAG_READ_ONLY,
};

static const struct v4l2_ctrl_config lt6911uxe_ctrl_audio_present = {
	.id = RK_V4L2_CID_AUDIO_PRESENT,
	.name = "Audio present",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 0,
	.flags = V4L2_CTRL_FLAG_READ_ONLY,
};

static void lt6911uxe_reset(struct lt6911uxe *lt6911uxe)
{
	gpiod_set_value(lt6911uxe->reset_gpio, 0);
	usleep_range(2000, 2100);
	gpiod_set_value(lt6911uxe->reset_gpio, 1);
	usleep_range(120*1000, 121*1000);
	gpiod_set_value(lt6911uxe->reset_gpio, 0);
	usleep_range(300*1000, 310*1000);
}

static int lt6911uxe_init_v4l2_ctrls(struct lt6911uxe *lt6911uxe)
{
	const struct lt6911uxe_mode *mode;
	struct v4l2_subdev *sd;
	int ret;

	mode = lt6911uxe->cur_mode;
	sd = &lt6911uxe->sd;
	ret = v4l2_ctrl_handler_init(&lt6911uxe->hdl, 5);
	if (ret)
		return ret;

	lt6911uxe->link_freq = v4l2_ctrl_new_int_menu(&lt6911uxe->hdl, NULL,
			V4L2_CID_LINK_FREQ,
			ARRAY_SIZE(link_freq_menu_items) - 1, 0,
			link_freq_menu_items);
	lt6911uxe->pixel_rate = v4l2_ctrl_new_std(&lt6911uxe->hdl, NULL,
			V4L2_CID_PIXEL_RATE,
			0, LT6911UXE_PIXEL_RATE, 1, LT6911UXE_PIXEL_RATE);

	lt6911uxe->detect_tx_5v_ctrl = v4l2_ctrl_new_std(&lt6911uxe->hdl,
			NULL, V4L2_CID_DV_RX_POWER_PRESENT,
			0, 1, 0, 0);

	lt6911uxe->audio_sampling_rate_ctrl =
		v4l2_ctrl_new_custom(&lt6911uxe->hdl,
				&lt6911uxe_ctrl_audio_sampling_rate, NULL);
	lt6911uxe->audio_present_ctrl = v4l2_ctrl_new_custom(&lt6911uxe->hdl,
			&lt6911uxe_ctrl_audio_present, NULL);

	sd->ctrl_handler = &lt6911uxe->hdl;
	if (lt6911uxe->hdl.error) {
		ret = lt6911uxe->hdl.error;
		v4l2_err(sd, "cfg v4l2 ctrls failed! ret:%d\n", ret);
		return ret;
	}

	__v4l2_ctrl_s_ctrl(lt6911uxe->link_freq, mode->mipi_freq_idx);
	__v4l2_ctrl_s_ctrl_int64(lt6911uxe->pixel_rate, LT6911UXE_PIXEL_RATE);

	if (lt6911uxe_update_controls(sd)) {
		ret = -ENODEV;
		v4l2_err(sd, "update v4l2 ctrls failed! ret:%d\n", ret);
		return ret;
	}

	return 0;
}

#ifdef CONFIG_OF
static int lt6911uxe_probe_of(struct lt6911uxe *lt6911uxe)
{
	struct device *dev = &lt6911uxe->i2c_client->dev;
	struct device_node *node = dev->of_node;
	struct device_node *ep;
	int ret;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
			&lt6911uxe->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
			&lt6911uxe->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
			&lt6911uxe->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
			&lt6911uxe->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	lt6911uxe->power_gpio = devm_gpiod_get_optional(dev, "power",
			GPIOD_OUT_LOW);
	if (IS_ERR(lt6911uxe->power_gpio)) {
		dev_err(dev, "failed to get power gpio\n");
		ret = PTR_ERR(lt6911uxe->power_gpio);
		return ret;
	}

	lt6911uxe->reset_gpio = devm_gpiod_get_optional(dev, "reset",
			GPIOD_OUT_HIGH);
	if (IS_ERR(lt6911uxe->reset_gpio)) {
		dev_err(dev, "failed to get reset gpio\n");
		ret = PTR_ERR(lt6911uxe->reset_gpio);
		return ret;
	}

	lt6911uxe->plugin_det_gpio = devm_gpiod_get_optional(dev, "plugin-det",
			GPIOD_IN);
	if (IS_ERR(lt6911uxe->plugin_det_gpio)) {
		dev_err(dev, "failed to get plugin det gpio\n");
		ret = PTR_ERR(lt6911uxe->plugin_det_gpio);
		return ret;
	}

	ep = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!ep) {
		dev_err(dev, "missing endpoint node\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(ep),
					&lt6911uxe->bus_cfg);
	if (ret) {
		dev_err(dev, "failed to parse endpoint\n");
		goto put_node;
	}

	lt6911uxe->support_modes = supported_modes_dphy;
	lt6911uxe->cfg_num = ARRAY_SIZE(supported_modes_dphy);

	lt6911uxe->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(lt6911uxe->xvclk)) {
		dev_err(dev, "failed to get xvclk\n");
		ret = -EINVAL;
		goto put_node;
	}

	ret = clk_prepare_enable(lt6911uxe->xvclk);
	if (ret) {
		dev_err(dev, "Failed! to enable xvclk\n");
		goto put_node;
	}

	lt6911uxe->enable_hdcp = false;

	gpiod_set_value(lt6911uxe->power_gpio, 1);
	lt6911uxe_reset(lt6911uxe);

	ret = 0;

put_node:
	of_node_put(ep);
	return ret;
}
#else
static inline int lt6911uxe_probe_of(struct lt6911uxe *state)
{
	return -ENODEV;
}
#endif
static int lt6911uxe_check_chip_id(struct lt6911uxe *lt6911uxe)
{
	struct device *dev = &lt6911uxe->i2c_client->dev;
	struct v4l2_subdev *sd = &lt6911uxe->sd;
	u8 id_h, id_l;
	u32 chipid;
	int ret = 0;

	lt6911uxe_i2c_enable(sd);
	id_l  = i2c_rd8(sd, CHIPID_REGL);
	id_h  = i2c_rd8(sd, CHIPID_REGH);
	lt6911uxe_i2c_disable(sd);

	chipid = (id_h << 8) | id_l;
	if (chipid != LT6911UXE_CHIPID) {
		dev_err(dev, "chipid err, read:%#x, expect:%#x\n",
				chipid, LT6911UXE_CHIPID);
		return -EINVAL;
	}
	dev_info(dev, "check chipid ok, id:%#x", chipid);

	return ret;
}

static int lt6911uxe_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct v4l2_dv_timings default_timing =
				V4L2_DV_BT_CEA_640X480P59_94;
	struct lt6911uxe *lt6911uxe;
	struct v4l2_subdev *sd;
	struct device *dev = &client->dev;
	char facing[2];
	int err;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	lt6911uxe = devm_kzalloc(dev, sizeof(struct lt6911uxe), GFP_KERNEL);
	if (!lt6911uxe)
		return -ENOMEM;

	sd = &lt6911uxe->sd;
	lt6911uxe->i2c_client = client;
	lt6911uxe->mbus_fmt_code = LT6911UXE_MEDIA_BUS_FMT;

	err = lt6911uxe_probe_of(lt6911uxe);
	if (err) {
		v4l2_err(sd, "lt6911uxe_parse_of failed! err:%d\n", err);
		return err;
	}

	lt6911uxe->timings = default_timing;
	lt6911uxe->cur_mode = &lt6911uxe->support_modes[0];
	err = lt6911uxe_check_chip_id(lt6911uxe);
	if (err < 0)
		return err;

	mutex_init(&lt6911uxe->confctl_mutex);
	err = lt6911uxe_init_v4l2_ctrls(lt6911uxe);
	if (err)
		goto err_free_hdl;

	client->flags |= I2C_CLIENT_SCCB;
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	v4l2_i2c_subdev_init(sd, client, &lt6911uxe_ops);
	sd->internal_ops = &lt6911uxe_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
#endif

#if defined(CONFIG_MEDIA_CONTROLLER)
	lt6911uxe->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	err = media_entity_pads_init(&sd->entity, 1, &lt6911uxe->pad);
	if (err < 0) {
		v4l2_err(sd, "media entity init failed! err:%d\n", err);
		goto err_free_hdl;
	}
#endif
	memset(facing, 0, sizeof(facing));
	if (strcmp(lt6911uxe->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 lt6911uxe->module_index, facing,
		 LT6911UXE_NAME, dev_name(sd->dev));
	err = v4l2_async_register_subdev_sensor_common(sd);
	if (err < 0) {
		v4l2_err(sd, "v4l2 register subdev failed! err:%d\n", err);
		goto err_clean_entity;
	}

	INIT_DELAYED_WORK(&lt6911uxe->delayed_work_hotplug,
			lt6911uxe_delayed_work_hotplug);
	INIT_DELAYED_WORK(&lt6911uxe->delayed_work_res_change,
			lt6911uxe_delayed_work_res_change);

	if (lt6911uxe->i2c_client->irq) {
		v4l2_dbg(1, debug, sd, "cfg lt6911uxe irq!\n");
		err = devm_request_threaded_irq(dev,
				lt6911uxe->i2c_client->irq,
				NULL, lt6911uxe_res_change_irq_handler,
				IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				"lt6911uxe", lt6911uxe);
		if (err) {
			v4l2_err(sd, "request irq failed! err:%d\n", err);
			goto err_work_queues;
		}
	} else {
		v4l2_dbg(1, debug, sd, "no irq, cfg poll!\n");
		INIT_WORK(&lt6911uxe->work_i2c_poll, lt6911uxe_work_i2c_poll);
		timer_setup(&lt6911uxe->timer, lt6911uxe_irq_poll_timer, 0);
		lt6911uxe->timer.expires = jiffies +
				       msecs_to_jiffies(POLL_INTERVAL_MS);
		add_timer(&lt6911uxe->timer);
	}

	lt6911uxe->plugin_irq = gpiod_to_irq(lt6911uxe->plugin_det_gpio);
	if (lt6911uxe->plugin_irq < 0)
		dev_err(dev, "failed to get plugin det irq, maybe no use\n");

	err = devm_request_threaded_irq(dev, lt6911uxe->plugin_irq, NULL,
			plugin_detect_irq_handler, IRQF_TRIGGER_FALLING |
			IRQF_TRIGGER_RISING | IRQF_ONESHOT, "lt6911uxe",
			lt6911uxe);
	if (err)
		dev_err(dev, "failed to register plugin det irq (%d), maybe no use\n", err);

	err = v4l2_ctrl_handler_setup(sd->ctrl_handler);
	if (err) {
		v4l2_err(sd, "v4l2 ctrl handler setup failed! err:%d\n", err);
		goto err_work_queues;
	}
	enable_stream(sd, false);
	v4l2_info(sd, "%s found @ 0x%x (%s)\n", client->name,
			client->addr << 1, client->adapter->name);

	return 0;

err_work_queues:
	if (!lt6911uxe->i2c_client->irq)
		flush_work(&lt6911uxe->work_i2c_poll);
	cancel_delayed_work(&lt6911uxe->delayed_work_hotplug);
	cancel_delayed_work(&lt6911uxe->delayed_work_res_change);
err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_free_hdl:
	v4l2_ctrl_handler_free(&lt6911uxe->hdl);
	mutex_destroy(&lt6911uxe->confctl_mutex);
	return err;
}

static int lt6911uxe_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct lt6911uxe *lt6911uxe = to_lt6911uxe(sd);

	if (!lt6911uxe->i2c_client->irq) {
		del_timer_sync(&lt6911uxe->timer);
		flush_work(&lt6911uxe->work_i2c_poll);
	}
	cancel_delayed_work_sync(&lt6911uxe->delayed_work_hotplug);
	cancel_delayed_work_sync(&lt6911uxe->delayed_work_res_change);
	v4l2_async_unregister_subdev(sd);
	v4l2_device_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&lt6911uxe->hdl);
	mutex_destroy(&lt6911uxe->confctl_mutex);
	clk_disable_unprepare(lt6911uxe->xvclk);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id lt6911uxe_of_match[] = {
	{ .compatible = "lontium,lt6911uxe" },
	{},
};
MODULE_DEVICE_TABLE(of, lt6911uxe_of_match);
#endif

static struct i2c_driver lt6911uxe_driver = {
	.driver = {
		.name = LT6911UXE_NAME,
		.of_match_table = of_match_ptr(lt6911uxe_of_match),
	},
	.probe = lt6911uxe_probe,
	.remove = lt6911uxe_remove,
};

static int __init lt6911uxe_driver_init(void)
{
	return i2c_add_driver(&lt6911uxe_driver);
}

static void __exit lt6911uxe_driver_exit(void)
{
	i2c_del_driver(&lt6911uxe_driver);
}

device_initcall_sync(lt6911uxe_driver_init);
module_exit(lt6911uxe_driver_exit);

MODULE_DESCRIPTION("Lontium lt6911uxe HDMI to CSI-2 bridge driver");
MODULE_AUTHOR("Jianwei Fan <jianwei.fan@rock-chips.com>");
MODULE_LICENSE("GPL");

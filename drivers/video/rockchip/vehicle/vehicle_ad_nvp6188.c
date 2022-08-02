// SPDX-License-Identifier: GPL-2.0
/*
 * vehicle sensor nvp6188
 *
 * Copyright (C) 2022 Rockchip Electronics Co.Ltd
 * Authors:
 *      wpzz <randy.wang@rock-chips.com>
 *      Jianwei Fan <jianwei.fan@rock-chips.com>
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/sysctl.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/suspend.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/uaccess.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include "vehicle_cfg.h"
#include "vehicle_main.h"
#include "vehicle_ad.h"
#include "vehicle_ad_nvp6188.h"

enum {
	CVSTD_720P60 = 0,
	CVSTD_720P50,
	CVSTD_1080P30,
	CVSTD_1080P25,
	CVSTD_720P30,
	CVSTD_720P25,
	CVSTD_SVGAP30,
	CVSTD_SD,
	CVSTD_NTSC,
	CVSTD_PAL
};

enum {
	FORCE_PAL_WIDTH = 960,
	FORCE_PAL_HEIGHT = 576,
	FORCE_NTSC_WIDTH = 960,
	FORCE_NTSC_HEIGHT = 480,
	FORCE_SVGA_WIDTH = 800,
	FORCE_SVGA_HEIGHT = 600,
	FORCE_720P_WIDTH = 1280,
	FORCE_720P_HEIGHT = 720,
	FORCE_1080P_WIDTH = 1920,
	FORCE_1080P_HEIGHT = 1080,
	FORCE_CIF_OUTPUT_FORMAT = CIF_OUTPUT_FORMAT_420,
};

enum {
	VIDEO_UNPLUG,
	VIDEO_IN,
	VIDEO_LOCKED,
	VIDEO_UNLOCK
};

#define NVP6188_LINK_FREQ_1458M			(1458000000UL >> 1)

static struct vehicle_ad_dev *nvp6188_g_addev;
static int cvstd_mode = CVSTD_1080P25;
//static int cvstd_old = CVSTD_720P25;
static int cvstd_state = VIDEO_UNPLUG;
// static int cvstd_old_state = VIDEO_UNLOCK;

static bool g_nvp6188_streaming;

#define NVP6188_CHIP_ID		0xD3
#define NVP6188_CHIP_ID2	0xD0

#define _MIPI_PORT0_
#ifdef _MIPI_PORT0_
#define _MAR_BANK_ 0x20
#define _MTX_BANK_ 0x23
#else
#define _MAR_BANK_ 0x30
#define _MTX_BANK_ 0x33
#endif

#define NVP_RESO_960H_NSTC_VALUE	0x00
#define NVP_RESO_960H_PAL_VALUE		0x10
#define NVP_RESO_720P_NSTC_VALUE	0x20
#define NVP_RESO_720P_PAL_VALUE		0x21
#define NVP_RESO_1080P_NSTC_VALUE	0x30
#define NVP_RESO_1080P_PAL_VALUE	0x31
#define NVP_RESO_960P_NSTC_VALUE	0xa0
#define NVP_RESO_960P_PAL_VALUE		0xa1

enum nvp6188_support_reso {
	NVP_RESO_UNKNOWN = 0,
	NVP_RESO_960H_PAL,
	NVP_RESO_720P_PAL,
	NVP_RESO_960P_PAL,
	NVP_RESO_1080P_PAL,
	NVP_RESO_960H_NSTC,
	NVP_RESO_720P_NSTC,
	NVP_RESO_960P_NSTC,
	NVP_RESO_1080P_NSTC,
};

struct regval {
	u8 addr;
	u8 val;
};

static __maybe_unused const struct regval common_setting_1458M_regs[] = {
	{0xff, 0x00},
	{0x80, 0x0f},
	{0x00, 0x10},
	{0x01, 0x10},
	{0x02, 0x10},
	{0x03, 0x10},
	{0x22, 0x0b},
	{0x23, 0x41},
	{0x26, 0x0b},
	{0x27, 0x41},
	{0x2a, 0x0b},
	{0x2b, 0x41},
	{0x2e, 0x0b},
	{0x2f, 0x41},

	{0xff, 0x01},
	{0x98, 0x30},
	{0xed, 0x00},

	{0xff, 0x05+0},
	{0x00, 0xd0},
	{0x01, 0x22},
	{0x47, 0xee},
	{0x50, 0xc6},
	{0x57, 0x00},
	{0x58, 0x77},
	{0x5b, 0x41},
	{0x5c, 0x78},
	{0xB8, 0xB8},

	{0xff, 0x05+1},
	{0x00, 0xd0},
	{0x01, 0x22},
	{0x47, 0xee},
	{0x50, 0xc6},
	{0x57, 0x00},
	{0x58, 0x77},
	{0x5b, 0x41},
	{0x5c, 0x78},
	{0xB8, 0xB8},

	{0xff, 0x05+2},
	{0x00, 0xd0},
	{0x01, 0x22},
	{0x47, 0xee},
	{0x50, 0xc6},
	{0x57, 0x00},
	{0x58, 0x77},
	{0x5b, 0x41},
	{0x5c, 0x78},
	{0xB8, 0xB8},

	{0xff, 0x05+3},
	{0x00, 0xd0},
	{0x01, 0x22},
	{0x47, 0xee},
	{0x50, 0xc6},
	{0x57, 0x00},
	{0x58, 0x77},
	{0x5b, 0x41},
	{0x5c, 0x78},
	{0xB8, 0xB8},

	{0xff, 0x09},
	{0x50, 0x30},
	{0x51, 0x6f},
	{0x52, 0x67},
	{0x53, 0x48},
	{0x54, 0x30},
	{0x55, 0x6f},
	{0x56, 0x67},
	{0x57, 0x48},
	{0x58, 0x30},
	{0x59, 0x6f},
	{0x5a, 0x67},
	{0x5b, 0x48},
	{0x5c, 0x30},
	{0x5d, 0x6f},
	{0x5e, 0x67},
	{0x5f, 0x48},

	{0xff, 0x0a},
	{0x25, 0x10},
	{0x27, 0x1e},
	{0x30, 0xac},
	{0x31, 0x78},
	{0x32, 0x17},
	{0x33, 0xc1},
	{0x34, 0x40},
	{0x35, 0x00},
	{0x36, 0xc3},
	{0x37, 0x0a},
	{0x38, 0x00},
	{0x39, 0x02},
	{0x3a, 0x00},
	{0x3b, 0xb2},
	{0xa5, 0x10},
	{0xa7, 0x1e},
	{0xb0, 0xac},
	{0xb1, 0x78},
	{0xb2, 0x17},
	{0xb3, 0xc1},
	{0xb4, 0x40},
	{0xb5, 0x00},
	{0xb6, 0xc3},
	{0xb7, 0x0a},
	{0xb8, 0x00},
	{0xb9, 0x02},
	{0xba, 0x00},
	{0xbb, 0xb2},
	{0xff, 0x0b},
	{0x25, 0x10},
	{0x27, 0x1e},
	{0x30, 0xac},
	{0x31, 0x78},
	{0x32, 0x17},
	{0x33, 0xc1},
	{0x34, 0x40},
	{0x35, 0x00},
	{0x36, 0xc3},
	{0x37, 0x0a},
	{0x38, 0x00},
	{0x39, 0x02},
	{0x3a, 0x00},
	{0x3b, 0xb2},
	{0xa5, 0x10},
	{0xa7, 0x1e},
	{0xb0, 0xac},
	{0xb1, 0x78},
	{0xb2, 0x17},
	{0xb3, 0xc1},
	{0xb4, 0x40},
	{0xb5, 0x00},
	{0xb6, 0xc3},
	{0xb7, 0x0a},
	{0xb8, 0x00},
	{0xb9, 0x02},
	{0xba, 0x00},
	{0xbb, 0xb2},

	{0xff, 0x13},
	{0x05, 0xa0},
	{0x31, 0xff},
	{0x07, 0x47},
	{0x12, 0x04},
	{0x1e, 0x1f},
	{0x1f, 0x27},
	{0x2e, 0x10},
	{0x2f, 0xc8},
	{0x31, 0xff},
	{0x32, 0x00},
	{0x33, 0x00},
	{0x72, 0x05},
	{0x7a, 0xf0},
	{0xff, _MAR_BANK_},
	{0x10, 0xff},
	{0x11, 0xff},

	{0x30, 0x0f},
	{0x32, 0xff},
	{0x34, 0xcd},
	{0x36, 0x04},
	{0x38, 0xff},
	{0x3c, 0x01},
	{0x3d, 0x11},
	{0x3e, 0x11},
	{0x45, 0x60},
	{0x46, 0x49},

	{0xff, _MTX_BANK_},
	{0xe9, 0x03},
	{0x03, 0x02},
	{0x01, 0xe4},
	{0x00, 0x7d},
	{0x01, 0xe0},
	{0x02, 0xa0},
	{0x20, 0x1e},
	{0x20, 0x1f},
	{0x04, 0x6c},
	{0x45, 0xcd},
	{0x46, 0x42},
	{0x47, 0x36},
	{0x48, 0x0f},
	{0x65, 0xcd},
	{0x66, 0x42},
	{0x67, 0x0e},
	{0x68, 0x0f},
	{0x85, 0xcd},
	{0x86, 0x42},
	{0x87, 0x0e},
	{0x88, 0x0f},
	{0xa5, 0xcd},
	{0xa6, 0x42},
	{0xa7, 0x0e},
	{0xa8, 0x0f},
	{0xc5, 0xcd},
	{0xc6, 0x42},
	{0xc7, 0x0e},
	{0xc8, 0x0f},
	{0xeb, 0x8d},

	{0xff, _MAR_BANK_},
	{0x00, 0xff},
	{0x40, 0x01},
	{0x40, 0x00},
	{0xff, 0x01},
	{0x97, 0x00},
	{0x97, 0x0f},

	{0xff, 0x00},  //test pattern
	{0x78, 0xba},
	{0x79, 0xac},
	{0xff, 0x05},
	{0x2c, 0x08},
	{0x6a, 0x80},
	{0xff, 0x06},
	{0x2c, 0x08},
	{0x6a, 0x80},
	{0xff, 0x07},
	{0x2c, 0x08},
	{0x6a, 0x80},
	{0xff, 0x08},
	{0x2c, 0x08},
	{0x6a, 0x80},
};

static __maybe_unused const struct regval auto_detect_regs[] = {
	{0xFF, 0x13},
	{0x30, 0x7f},
	{0x70, 0xf0},

	{0xFF, 0x00},
	{0x00, 0x18},
	{0x01, 0x18},
	{0x02, 0x18},
	{0x03, 0x18},

	{0x00, 0x10},
	{0x01, 0x10},
	{0x02, 0x10},
	{0x03, 0x10},
};

static void nvp6188_reinit_parameter(struct vehicle_ad_dev *ad, unsigned char cvstd)
{
	int i = 0;

	switch (cvstd) {
	case CVSTD_720P25:
		ad->cfg.width = 1280;
		ad->cfg.height = 720;
		ad->cfg.start_x = 0;
		ad->cfg.start_y = 0;
		ad->cfg.input_format = CIF_INPUT_FORMAT_YUV;
		ad->cfg.output_format = FORCE_CIF_OUTPUT_FORMAT;
		ad->cfg.field_order = 0;
		ad->cfg.yuv_order = 0;/*00 - UYVY*/
		ad->cfg.href = 0;
		ad->cfg.vsync = 0;
		ad->cfg.frame_rate = 25;
		ad->cfg.mipi_freq = NVP6188_LINK_FREQ_1458M;
		break;

	case CVSTD_1080P25:
		ad->cfg.width = 1920;
		ad->cfg.height = 1080;
		ad->cfg.start_x = 0;
		ad->cfg.start_y = 0;
		ad->cfg.input_format = CIF_INPUT_FORMAT_YUV;
		ad->cfg.output_format = FORCE_CIF_OUTPUT_FORMAT;
		ad->cfg.field_order = 0;
		ad->cfg.yuv_order = 0;/*00 - UYVY*/
		ad->cfg.href = 0;
		ad->cfg.vsync = 0;
		ad->cfg.frame_rate = 25;
		ad->cfg.mipi_freq = NVP6188_LINK_FREQ_1458M;
		break;

	case CVSTD_NTSC:
		ad->cfg.width = 960;
		ad->cfg.height = 480;
		ad->cfg.start_x = 0;
		ad->cfg.start_y = 0;
		ad->cfg.input_format = CIF_INPUT_FORMAT_YUV;
		ad->cfg.output_format = FORCE_CIF_OUTPUT_FORMAT;
		ad->cfg.field_order = 0;
		ad->cfg.yuv_order = 0;/*00 - UYVY*/
		ad->cfg.href = 0;
		ad->cfg.vsync = 0;
		ad->cfg.frame_rate = 25;
		ad->cfg.mipi_freq = NVP6188_LINK_FREQ_1458M;
		break;

	default:
		ad->cfg.width = 1920;
		ad->cfg.height = 1080;
		ad->cfg.start_x = 0;
		ad->cfg.start_y = 0;
		ad->cfg.input_format = CIF_INPUT_FORMAT_YUV;
		ad->cfg.output_format = FORCE_CIF_OUTPUT_FORMAT;
		ad->cfg.field_order = 0;
		ad->cfg.yuv_order = 0;/*00 - UYVY*/
		ad->cfg.href = 0;
		ad->cfg.vsync = 0;
		ad->cfg.frame_rate = 25;
		ad->cfg.mipi_freq = NVP6188_LINK_FREQ_1458M;
		break;
	}
	ad->cfg.type = V4L2_MBUS_CSI2_DPHY;
	ad->cfg.mbus_flags = V4L2_MBUS_CSI2_4_LANE | V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK |
			 V4L2_MBUS_CSI2_CHANNELS;
	ad->cfg.mbus_code = MEDIA_BUS_FMT_UYVY8_2X8;

	switch (ad->cfg.mbus_flags & V4L2_MBUS_CSI2_LANES) {
	case V4L2_MBUS_CSI2_1_LANE:
		ad->cfg.lanes = 1;
		break;
	case V4L2_MBUS_CSI2_2_LANE:
		ad->cfg.lanes = 2;
		break;
	case V4L2_MBUS_CSI2_3_LANE:
		ad->cfg.lanes = 3;
		break;
	case V4L2_MBUS_CSI2_4_LANE:
		ad->cfg.lanes = 4;
		break;
	default:
		ad->cfg.lanes = 1;
		break;
	}

	/* fix crop info from dts config */
	for (i = 0; i < 4; i++) {
		if ((ad->defrects[i].width == ad->cfg.width) &&
		    (ad->defrects[i].height == ad->cfg.height)) {
			ad->cfg.start_x = ad->defrects[i].crop_x;
			ad->cfg.start_y = ad->defrects[i].crop_y;
			ad->cfg.width = ad->defrects[i].crop_width;
			ad->cfg.height = ad->defrects[i].crop_height;
		}
	}
}

/* sensor register write */
static int nvp6188_write_reg(struct vehicle_ad_dev *ad, u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[2];
	int ret;

	buf[0] = reg & 0xFF;
	buf[1] = val;

	msg.addr = ad->i2c_add;
	msg.flags = 0;
	msg.buf = buf;
	msg.len = sizeof(buf);

	ret = i2c_transfer(ad->adapter, &msg, 1);
	if (ret >= 0) {
		usleep_range(300, 400);
		return 0;
	}

	VEHICLE_DGERR("nvp6188 write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

static int nvp6188_write_array(struct vehicle_ad_dev *ad,
			       const struct regval *regs, int size)
{
	int i, ret = 0;

	i = 0;
	while (i < size) {
		ret = nvp6188_write_reg(ad, regs[i].addr, regs[i].val);
		if (ret) {
			VEHICLE_DGERR("%s failed !\n", __func__);
			break;
		}
		i++;
	}

	return ret;
}

/* sensor register read */
static int nvp6188_read_reg(struct vehicle_ad_dev *ad, u8 reg, u8 *val)
{
	struct i2c_msg msg[2];
	u8 buf[1];
	int ret;

	buf[0] = reg & 0xFF;

	msg[0].addr = ad->i2c_add;
	msg[0].flags = 0;
	msg[0].buf = buf;
	msg[0].len = sizeof(buf);

	msg[1].addr = ad->i2c_add;
	msg[1].flags = 0 | I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = 1;

	ret = i2c_transfer(ad->adapter, msg, 2);
	if (ret >= 0) {
		*val = buf[0];
		return 0;
	}

	VEHICLE_DGERR("nvp6188 read reg(0x%x) failed !\n", reg);

	return ret;
}

static unsigned char nv6188_read_vfc(struct vehicle_ad_dev *ad, unsigned char ch)
{
	unsigned char ch_vfc = 0xff;

	nvp6188_write_reg(ad, 0xff, 0x05 + ch);
	nvp6188_read_reg(ad, 0xf0, &ch_vfc);
	return ch_vfc;
}

static __maybe_unused int nvp6188_read_all_vfc(struct vehicle_ad_dev *ad,
					       u8 *ch_vfc)
{
	int ret = 0;
	int check_cnt = 0, ch = 0;

	ret = nvp6188_write_array(ad,
		auto_detect_regs, ARRAY_SIZE(auto_detect_regs));
	if (ret)
		VEHICLE_DGERR("write auto_detect_regs failed %d", ret);

	ret = -1;
	while ((check_cnt++) < 50) {
		for (ch = 0; ch < 4; ch++)
			ch_vfc[ch] = nv6188_read_vfc(ad, ch);

		if (ch_vfc[0] != 0xff || ch_vfc[1] != 0xff ||
		    ch_vfc[2] != 0xff || ch_vfc[3] != 0xff) {
			ret = 0;
			if (ch == 3) {
				VEHICLE_DGERR("try check cnt %d", check_cnt);
				break;
			}
		} else {
			usleep_range(20 * 1000, 40 * 1000);
		}
	}

	if (ret)
		VEHICLE_DGERR("read vfc failed %d", ret);
	else
		VEHICLE_INFO("read vfc 0x%2x 0x%2x 0x%2x 0x%2x",
				ch_vfc[0], ch_vfc[1], ch_vfc[2], ch_vfc[3]);

	return ret;
}

static __maybe_unused int nvp6188_auto_detect_fmt(struct vehicle_ad_dev *ad)
{
	int ret = 0;
	int ch = 0;
	unsigned char ch_vfc[4] = { 0xff, 0xff, 0xff, 0xff };
	unsigned char val_13x70 = 0, val_13x71 = 0;

	if (nvp6188_read_all_vfc(ad, ch_vfc))
		return -1;
	ch = ad->ad_chl;
	// for (ch = 0; ch < 4; ch++) {
		nvp6188_write_reg(ad, 0xFF, 0x13);
		nvp6188_read_reg(ad, 0x70, &val_13x70);
		val_13x70 |= (0x01 << ch);
		nvp6188_write_reg(ad, 0x70, val_13x70);
		nvp6188_read_reg(ad, 0x71, &val_13x71);
		val_13x71 |= (0x01 << ch);
		nvp6188_write_reg(ad, 0x71, val_13x71);
		switch (ch_vfc[ch]) {
		case NVP_RESO_960H_NSTC_VALUE:
			VEHICLE_INFO("channel %d det 960h nstc", ch);
			ad->channel_reso[ch] = NVP_RESO_960H_NSTC;
		break;
		case NVP_RESO_960H_PAL_VALUE:
			VEHICLE_INFO("channel %d det 960h pal", ch);
			ad->channel_reso[ch] = NVP_RESO_960H_PAL;
		break;
		case NVP_RESO_720P_NSTC_VALUE:
			VEHICLE_INFO("channel %d det 720p nstc", ch);
			ad->channel_reso[ch] = NVP_RESO_720P_NSTC;
		break;
		case NVP_RESO_720P_PAL_VALUE:
			VEHICLE_INFO("channel %d det 720p pal", ch);
			ad->channel_reso[ch] = NVP_RESO_720P_PAL;
		break;
		case NVP_RESO_1080P_NSTC_VALUE:
			VEHICLE_INFO("channel %d det 1080p nstc", ch);
			ad->channel_reso[ch] = NVP_RESO_1080P_NSTC;
		break;
		case NVP_RESO_1080P_PAL_VALUE:
			VEHICLE_INFO("channel %d det 1080p pal", ch);
			ad->channel_reso[ch] = NVP_RESO_1080P_PAL;
		break;
		case NVP_RESO_960P_NSTC_VALUE:
			VEHICLE_INFO("channel %d det 960p nstc", ch);
			ad->channel_reso[ch] = NVP_RESO_960P_NSTC;
		break;
		case NVP_RESO_960P_PAL_VALUE:
			VEHICLE_INFO("channel %d det 960p pal", ch);
			ad->channel_reso[ch] = NVP_RESO_960P_PAL;
		break;
		default:
			VEHICLE_INFO("channel %d not detect, def 1080p pal\n", ch);
			ad->channel_reso[ch] = NVP_RESO_1080P_PAL;
		break;
		}
	// }
	return ret;
}

//each channel setting
/*
 * 960x480i
 * ch : 0 ~ 3
 * ntpal: 1:25p, 0:30p
 */
static __maybe_unused void nv6188_set_chn_960h(struct vehicle_ad_dev *ad, u8 ch,
					       u8 ntpal)
{
	unsigned char val_0x54 = 0, val_20x01 = 0;

	VEHICLE_INFO("%s ch %d ntpal %d", __func__, ch, ntpal);
	nvp6188_write_reg(ad, 0xff, 0x00);
	nvp6188_write_reg(ad, 0x08 + ch, ntpal ? 0xdd : 0xa0);
	nvp6188_write_reg(ad, 0x18 + ch, 0x08);
	nvp6188_write_reg(ad, 0x22 + ch * 4, 0x0b);
	nvp6188_write_reg(ad, 0x23 + ch * 4, 0x41);
	nvp6188_write_reg(ad, 0x30 + ch, 0x12);
	nvp6188_write_reg(ad, 0x34 + ch, 0x01);
	nvp6188_read_reg(ad, 0x54, &val_0x54);
	if (ntpal)
		val_0x54 &= ~(0x10 << ch);
	else
		val_0x54 |= (0x10 << ch);
	nvp6188_write_reg(ad, 0x54, val_0x54);
	nvp6188_write_reg(ad, 0x58 + ch, ntpal ? 0x80 : 0x90);
	nvp6188_write_reg(ad, 0x5c + ch, ntpal ? 0xbe : 0xbc);
	nvp6188_write_reg(ad, 0x64 + ch, ntpal ? 0xa0 : 0x81);
	nvp6188_write_reg(ad, 0x81 + ch, ntpal ? 0xf0 : 0xe0);
	nvp6188_write_reg(ad, 0x85 + ch, 0x00);
	nvp6188_write_reg(ad, 0x89 + ch, 0x00);
	nvp6188_write_reg(ad, ch + 0x8e, 0x00);
	nvp6188_write_reg(ad, 0xa0 + ch, 0x05);

	nvp6188_write_reg(ad, 0xff, 0x01);
	nvp6188_write_reg(ad, 0x84 + ch, 0x02);
	nvp6188_write_reg(ad, 0x88 + ch, 0x00);
	nvp6188_write_reg(ad, 0x8c + ch, 0x40);
	nvp6188_write_reg(ad, 0xa0 + ch, 0x20);
	nvp6188_write_reg(ad, 0xed, 0x00);

	nvp6188_write_reg(ad, 0xff, 0x05 + ch);
	nvp6188_write_reg(ad, 0x01, 0x22);
	nvp6188_write_reg(ad, 0x05, 0x00);
	nvp6188_write_reg(ad, 0x08, 0x55);
	nvp6188_write_reg(ad, 0x25, 0xdc);
	nvp6188_write_reg(ad, 0x28, 0x80);
	nvp6188_write_reg(ad, 0x2f, 0x00);
	nvp6188_write_reg(ad, 0x30, 0xe0);
	nvp6188_write_reg(ad, 0x31, 0x43);
	nvp6188_write_reg(ad, 0x32, 0xa2);
	nvp6188_write_reg(ad, 0x47, 0x04);
	nvp6188_write_reg(ad, 0x50, 0x84);
	nvp6188_write_reg(ad, 0x57, 0x00);
	nvp6188_write_reg(ad, 0x58, 0x77);
	nvp6188_write_reg(ad, 0x5b, 0x43);
	nvp6188_write_reg(ad, 0x5c, 0x78);
	nvp6188_write_reg(ad, 0x5f, 0x00);
	nvp6188_write_reg(ad, 0x62, 0x20);
	nvp6188_write_reg(ad, 0x7b, 0x00);
	nvp6188_write_reg(ad, 0x7c, 0x01);
	nvp6188_write_reg(ad, 0x7d, 0x80);
	nvp6188_write_reg(ad, 0x80, 0x00);
	nvp6188_write_reg(ad, 0x90, 0x01);
	nvp6188_write_reg(ad, 0xa9, 0x00);
	nvp6188_write_reg(ad, 0xb5, 0x00);
	nvp6188_write_reg(ad, 0xb8, 0xb9);
	nvp6188_write_reg(ad, 0xb9, 0x72);
	nvp6188_write_reg(ad, 0xd1, 0x00);
	nvp6188_write_reg(ad, 0xd5, 0x80);

	nvp6188_write_reg(ad, 0xff, 0x09);
	nvp6188_write_reg(ad, 0x96 + ch * 0x20, 0x10);
	nvp6188_write_reg(ad, 0x98 + ch * 0x20, ntpal ? 0xc0 : 0xe0);
	nvp6188_write_reg(ad, ch * 0x20 + 0x9e, 0x00);

	nvp6188_write_reg(ad, 0xff, _MAR_BANK_);
	nvp6188_read_reg(ad, 0x01, &val_20x01);
	val_20x01 &= (~(0x03 << (ch * 2)));
	val_20x01 |= (0x02 << (ch * 2));
	nvp6188_write_reg(ad, 0x01, val_20x01);
	nvp6188_write_reg(ad, 0x12 + ch * 2, 0xe0);
	nvp6188_write_reg(ad, 0x13 + ch * 2, 0x01);
}

//each channel setting
/*
 * 1280x720p
 * ch : 0 ~ 3
 * ntpal: 1:25p, 0:30p
 */
static __maybe_unused void nv6188_set_chn_720p(struct vehicle_ad_dev *ad, u8 ch,
					       u8 ntpal)
{
	unsigned char val_0x54 = 0, val_20x01 = 0;

	VEHICLE_INFO("%s ch %d ntpal %d", __func__, ch, ntpal);
	nvp6188_write_reg(ad, 0xff, 0x00);
	nvp6188_write_reg(ad, 0x08 + ch, 0x00);
	nvp6188_write_reg(ad, 0x18 + ch, 0x3f);
	nvp6188_write_reg(ad, 0x30 + ch, 0x12);
	nvp6188_write_reg(ad, 0x34 + ch, 0x00);
	nvp6188_read_reg(ad, 0x54, &val_0x54);
	val_0x54 &= ~(0x10 << ch);
	nvp6188_write_reg(ad, 0x54, val_0x54);
	nvp6188_write_reg(ad, 0x58 + ch, ntpal ? 0x80 : 0x80);
	nvp6188_write_reg(ad, 0x5c + ch, ntpal ? 0x00 : 0x00);
	nvp6188_write_reg(ad, 0x64 + ch, ntpal ? 0x01 : 0x01);
	nvp6188_write_reg(ad, 0x81 + ch, ntpal ? 0x0d : 0x0c);
	nvp6188_write_reg(ad, 0x85 + ch, 0x00);
	nvp6188_write_reg(ad, 0x89 + ch, 0x00);
	nvp6188_write_reg(ad, ch + 0x8e, 0x00);
	nvp6188_write_reg(ad, 0xa0 + ch, 0x05);

	nvp6188_write_reg(ad, 0xff, 0x01);
	nvp6188_write_reg(ad, 0x84 + ch, 0x02);
	nvp6188_write_reg(ad, 0x88 + ch, 0x00);
	nvp6188_write_reg(ad, 0x8c + ch, 0x40);
	nvp6188_write_reg(ad, 0xa0 + ch, 0x20);

	nvp6188_write_reg(ad, 0xff, 0x05 + ch);
	nvp6188_write_reg(ad, 0x01, 0x22);
	nvp6188_write_reg(ad, 0x05, 0x04);
	nvp6188_write_reg(ad, 0x08, 0x55);
	nvp6188_write_reg(ad, 0x25, 0xdc);
	nvp6188_write_reg(ad, 0x28, 0x80);
	nvp6188_write_reg(ad, 0x2f, 0x00);
	nvp6188_write_reg(ad, 0x30, 0xe0);
	nvp6188_write_reg(ad, 0x31, 0x43);
	nvp6188_write_reg(ad, 0x32, 0xa2);
	nvp6188_write_reg(ad, 0x47, 0xee);
	nvp6188_write_reg(ad, 0x50, 0xc6);
	nvp6188_write_reg(ad, 0x57, 0x00);
	nvp6188_write_reg(ad, 0x58, 0x77);
	nvp6188_write_reg(ad, 0x5b, 0x41);
	nvp6188_write_reg(ad, 0x5c, 0x7C);
	nvp6188_write_reg(ad, 0x5f, 0x00);
	nvp6188_write_reg(ad, 0x62, 0x20);
	nvp6188_write_reg(ad, 0x7b, 0x11);
	nvp6188_write_reg(ad, 0x7c, 0x01);
	nvp6188_write_reg(ad, 0x7d, 0x80);
	nvp6188_write_reg(ad, 0x80, 0x00);
	nvp6188_write_reg(ad, 0x90, 0x01);
	nvp6188_write_reg(ad, 0xa9, 0x00);
	nvp6188_write_reg(ad, 0xb5, 0x40);
	nvp6188_write_reg(ad, 0xb8, 0x39);
	nvp6188_write_reg(ad, 0xb9, 0x72);
	nvp6188_write_reg(ad, 0xd1, 0x00);
	nvp6188_write_reg(ad, 0xd5, 0x80);

	nvp6188_write_reg(ad, 0xff, 0x09);
	nvp6188_write_reg(ad, 0x96 + ch * 0x20, 0x00);
	nvp6188_write_reg(ad, 0x98 + ch * 0x20, 0x00);
	nvp6188_write_reg(ad, ch * 0x20 + 0x9e, 0x00);

	nvp6188_write_reg(ad, 0xff, _MAR_BANK_);
	nvp6188_read_reg(ad, 0x01, &val_20x01);
	val_20x01 &= (~(0x03 << (ch * 2)));
	val_20x01 |= (0x01 << (ch * 2));
	nvp6188_write_reg(ad, 0x01, val_20x01);
	nvp6188_write_reg(ad, 0x12 + ch * 2, 0x80);
	nvp6188_write_reg(ad, 0x13 + ch * 2, 0x02);
}

//each channel setting
/*
 * 1920x1080p
 * ch : 0 ~ 3
 * ntpal: 1:25p, 0:30p
 */
static __maybe_unused void nv6188_set_chn_1080p(struct vehicle_ad_dev *ad, u8 ch,
						u8 ntpal)
{
	unsigned char val_0x54 = 0, val_20x01 = 0;

	VEHICLE_INFO("%s ch %d ntpal %d", __func__, ch, ntpal);
	nvp6188_write_reg(ad, 0xff, 0x00);
	nvp6188_write_reg(ad, 0x08 + ch, 0x00);
	nvp6188_write_reg(ad, 0x18 + ch, 0x3f);
	nvp6188_write_reg(ad, 0x30 + ch, 0x12);
	nvp6188_write_reg(ad, 0x34 + ch, 0x00);
	nvp6188_read_reg(ad, 0x54, &val_0x54);
	val_0x54 &= ~(0x10 << ch);
	nvp6188_write_reg(ad, 0x54, val_0x54);
	nvp6188_write_reg(ad, 0x58 + ch, ntpal ? 0x80 : 0x80);
	nvp6188_write_reg(ad, 0x5c + ch, ntpal ? 0x00 : 0x00);
	nvp6188_write_reg(ad, 0x64 + ch, ntpal ? 0x01 : 0x01);
	nvp6188_write_reg(ad, 0x81 + ch, ntpal ? 0x03 : 0x02);
	nvp6188_write_reg(ad, 0x85 + ch, 0x00);
	nvp6188_write_reg(ad, 0x89 + ch, 0x10);
	nvp6188_write_reg(ad, ch + 0x8e, 0x00);
	nvp6188_write_reg(ad, 0xa0 + ch, 0x05);

	nvp6188_write_reg(ad, 0xff, 0x01);
	nvp6188_write_reg(ad, 0x84 + ch, 0x02);
	nvp6188_write_reg(ad, 0x88 + ch, 0x00);
	nvp6188_write_reg(ad, 0x8c + ch, 0x40);
	nvp6188_write_reg(ad, 0xa0 + ch, 0x20);

	nvp6188_write_reg(ad, 0xff, 0x05 + ch);
	nvp6188_write_reg(ad, 0x01, 0x22);
	nvp6188_write_reg(ad, 0x05, 0x04);
	nvp6188_write_reg(ad, 0x08, 0x55);
	nvp6188_write_reg(ad, 0x25, 0xdc);
	nvp6188_write_reg(ad, 0x28, 0x80);
	nvp6188_write_reg(ad, 0x2f, 0x00);
	nvp6188_write_reg(ad, 0x30, 0xe0);
	nvp6188_write_reg(ad, 0x31, 0x41);
	nvp6188_write_reg(ad, 0x32, 0xa2);
	nvp6188_write_reg(ad, 0x47, 0xee);
	nvp6188_write_reg(ad, 0x50, 0xc6);
	nvp6188_write_reg(ad, 0x57, 0x00);
	nvp6188_write_reg(ad, 0x58, 0x77);
	nvp6188_write_reg(ad, 0x5b, 0x41);
	nvp6188_write_reg(ad, 0x5c, 0x7C);
	nvp6188_write_reg(ad, 0x5f, 0x00);
	nvp6188_write_reg(ad, 0x62, 0x20);
	nvp6188_write_reg(ad, 0x7b, 0x11);
	nvp6188_write_reg(ad, 0x7c, 0x01);
	nvp6188_write_reg(ad, 0x7d, 0x80);
	nvp6188_write_reg(ad, 0x80, 0x00);
	nvp6188_write_reg(ad, 0x90, 0x01);
	nvp6188_write_reg(ad, 0xa9, 0x00);
	nvp6188_write_reg(ad, 0xb5, 0x40);
	nvp6188_write_reg(ad, 0xb8, 0x39);
	nvp6188_write_reg(ad, 0xb9, 0x72);
	nvp6188_write_reg(ad, 0xd1, 0x00);
	nvp6188_write_reg(ad, 0xd5, 0x80);

	nvp6188_write_reg(ad, 0xff, 0x09);
	nvp6188_write_reg(ad, 0x96 + ch * 0x20, 0x00);
	nvp6188_write_reg(ad, 0x98 + ch * 0x20, 0x00);
	nvp6188_write_reg(ad, ch * 0x20 + 0x9e, 0x00);

	nvp6188_write_reg(ad, 0xff, _MAR_BANK_);
	nvp6188_read_reg(ad, 0x01, &val_20x01);
	val_20x01 &= (~(0x03 << (ch * 2)));
	nvp6188_write_reg(ad, 0x01, val_20x01);
	nvp6188_write_reg(ad, 0x12 + ch * 2, 0xc0);
	nvp6188_write_reg(ad, 0x13 + ch * 2, 0x03);
}

static __maybe_unused void nvp6188_manual_mode(struct vehicle_ad_dev *ad)
{
	int i, reso;

	for (i = 3; i >= 0; i--) {
		reso = ad->channel_reso[i];
		switch (reso) {
		case NVP_RESO_960H_PAL:
			nv6188_set_chn_960h(ad, i, 1);
			break;
		case NVP_RESO_720P_PAL:
			nv6188_set_chn_720p(ad, i, 1);
			break;
		case NVP_RESO_1080P_PAL:
			nv6188_set_chn_1080p(ad, i, 1);
			break;
		case NVP_RESO_960H_NSTC:
			nv6188_set_chn_960h(ad, i, 0);
			break;
		case NVP_RESO_720P_NSTC:
			nv6188_set_chn_720p(ad, i, 0);
			break;
		case NVP_RESO_1080P_NSTC:
			nv6188_set_chn_1080p(ad, i, 0);
			break;
		default:
			nv6188_set_chn_1080p(ad, i, 1);
			break;
		}
	}
}

void nvp6188_channel_set(struct vehicle_ad_dev *ad, int channel)
{
	ad->ad_chl = channel;
	VEHICLE_DG("%s, channel set(%d)", __func__, ad->ad_chl);
}

int nvp6188_ad_get_cfg(struct vehicle_cfg **cfg)
{
	if (!nvp6188_g_addev)
		return -1;

	switch (cvstd_state) {
	case VIDEO_UNPLUG:
		nvp6188_g_addev->cfg.ad_ready = false;
		break;
	case VIDEO_LOCKED:
		nvp6188_g_addev->cfg.ad_ready = true;
		break;
	case VIDEO_IN:
		nvp6188_g_addev->cfg.ad_ready = false;
		break;
	}

	nvp6188_g_addev->cfg.ad_ready = true;

	*cfg = &nvp6188_g_addev->cfg;

	return 0;
}

void nvp6188_ad_check_cif_error(struct vehicle_ad_dev *ad, int last_line)
{
	VEHICLE_INFO("%s, last_line %d\n", __func__, last_line);

	if (last_line < 1)
		return;

	ad->cif_error_last_line = last_line;
	if (cvstd_mode == CVSTD_PAL) {
		if (last_line == FORCE_NTSC_HEIGHT) {
			if (ad->state_check_work.state_check_wq)
				queue_delayed_work(
					ad->state_check_work.state_check_wq,
					&ad->state_check_work.work,
					msecs_to_jiffies(0));
		}
	} else if (cvstd_mode == CVSTD_NTSC) {
		if (last_line == FORCE_PAL_HEIGHT) {
			if (ad->state_check_work.state_check_wq)
				queue_delayed_work(
					ad->state_check_work.state_check_wq,
					&ad->state_check_work.work,
					msecs_to_jiffies(0));
		}
	} else if (cvstd_mode == CVSTD_1080P25) {
		if (last_line == FORCE_1080P_HEIGHT) {
			if (ad->state_check_work.state_check_wq)
				queue_delayed_work(
					ad->state_check_work.state_check_wq,
					&ad->state_check_work.work,
					msecs_to_jiffies(0));
		}
	} else if (cvstd_mode == CVSTD_720P25) {
		if (last_line == FORCE_720P_HEIGHT) {
			if (ad->state_check_work.state_check_wq)
				queue_delayed_work(
					ad->state_check_work.state_check_wq,
					&ad->state_check_work.work,
					msecs_to_jiffies(0));
		}
	}
}

int nvp6188_check_id(struct vehicle_ad_dev *ad)
{
	int ret = 0;
	u8 pid = 0;

	ret = vehicle_sensor_write(ad, 0xFF, 0x00);
	ret |= vehicle_sensor_read(ad, 0xf4, &pid);
	if (ret)
		return ret;

	if (pid != NVP6188_CHIP_ID && pid != NVP6188_CHIP_ID2) {
		VEHICLE_DGERR("%s: expected 0xd0/d3, detected: 0x%02x !",
			ad->ad_name, pid);
		ret = -EINVAL;
	} else {
		VEHICLE_INFO("%s Found NVP6188 sensor: id(0x%2x) !\n", __func__, pid);
	}

	return ret;
}

static int __nvp6188_start_stream(struct vehicle_ad_dev *ad)
{
	int ret;
	int array_size = 0;

	array_size = ARRAY_SIZE(common_setting_1458M_regs);

	ret = nvp6188_write_array(ad,
		common_setting_1458M_regs, array_size);
	if (ret) {
		VEHICLE_INFO(" nvp6188 start stream: wrote global reg failed");
		return ret;
	}

	nvp6188_auto_detect_fmt(ad);
	nvp6188_manual_mode(ad);
	nvp6188_write_reg(ad, 0xff, 0x20);
	nvp6188_write_reg(ad, 0xff, 0xff);
	msleep(50);

	return 0;
}

static int __nvp6188_stop_stream(struct vehicle_ad_dev *ad)
{
	nvp6188_write_reg(ad, 0xff, 0x20);
	nvp6188_write_reg(ad, 0x00, 0x00);
	nvp6188_write_reg(ad, 0x40, 0x01);
	nvp6188_write_reg(ad, 0x40, 0x00);

	return 0;
}

int nvp6188_stream(struct vehicle_ad_dev *ad, int enable)
{
	VEHICLE_INFO("%s on(%d)\n", __func__, enable);

	g_nvp6188_streaming = (enable != 0);
	if (g_nvp6188_streaming) {
		__nvp6188_start_stream(ad);
		if (ad->state_check_work.state_check_wq)
			queue_delayed_work(ad->state_check_work.state_check_wq,
				&ad->state_check_work.work, msecs_to_jiffies(200));
	} else {
		__nvp6188_stop_stream(ad);
		if (ad->state_check_work.state_check_wq)
			cancel_delayed_work_sync(&ad->state_check_work.work);
		VEHICLE_DG("%s(%d): cancel_queue_delayed_work!\n", __func__, __LINE__);
	}

	return 0;
}

static void nvp6188_power_on(struct vehicle_ad_dev *ad)
{
	if (gpio_is_valid(ad->power)) {
		gpio_request(ad->power, "nvp6188_power");
		gpio_direction_output(ad->power, ad->pwr_active);
		/* gpio_set_value(ad->power, ad->pwr_active); */
	}

	if (gpio_is_valid(ad->powerdown)) {
		gpio_request(ad->powerdown, "nvp6188_pwd");
		gpio_direction_output(ad->powerdown, 1);
		/* gpio_set_value(ad->powerdown, !ad->pwdn_active); */
	}

	if (gpio_is_valid(ad->reset)) {
		gpio_request(ad->reset, "nvp6188_rst");
		gpio_direction_output(ad->reset, 0);
		usleep_range(1500, 2000);
		gpio_direction_output(ad->reset, 1);
	}
}

static void nvp6188_power_off(struct vehicle_ad_dev *ad)
{
	if (gpio_is_valid(ad->reset))
		gpio_free(ad->reset);
	if (gpio_is_valid(ad->power))
		gpio_free(ad->power);
	if (gpio_is_valid(ad->powerdown))
		gpio_free(ad->powerdown);
}

static __maybe_unused int nvp6188_auto_detect_hotplug(struct vehicle_ad_dev *ad)
{
	nvp6188_write_reg(ad, 0xff, 0x00);
	nvp6188_read_reg(ad, 0xa8, &ad->detect_status);

	ad->detect_status = ~ad->detect_status;

	return 0;
}

static void nvp6188_check_state_work(struct work_struct *work)
{
	struct vehicle_ad_dev *ad;

	ad = nvp6188_g_addev;
	nvp6188_auto_detect_hotplug(ad);

	if (ad->detect_status != ad->last_detect_status) {
		ad->last_detect_status = ad->detect_status;
		vehicle_ad_stat_change_notify();
	}

	if (g_nvp6188_streaming) {
		queue_delayed_work(ad->state_check_work.state_check_wq,
				   &ad->state_check_work.work, msecs_to_jiffies(100));
	}
}

int nvp6188_ad_deinit(void)
{
	struct vehicle_ad_dev *ad;

	ad = nvp6188_g_addev;

	if (!ad)
		return -1;

	if (ad->state_check_work.state_check_wq) {
		cancel_delayed_work_sync(&ad->state_check_work.work);
		flush_delayed_work(&ad->state_check_work.work);
		flush_workqueue(ad->state_check_work.state_check_wq);
		destroy_workqueue(ad->state_check_work.state_check_wq);
	}

	nvp6188_power_off(ad);

	return 0;
}

static __maybe_unused int get_ad_mode_from_fix_format(int fix_format)
{
	int mode = -1;

	switch (fix_format) {
	case AD_FIX_FORMAT_PAL:
	case AD_FIX_FORMAT_NTSC:
	case AD_FIX_FORMAT_720P_50FPS:
	case AD_FIX_FORMAT_720P_30FPS:
	case AD_FIX_FORMAT_720P_25FPS:
		mode = CVSTD_720P25;
		break;
	case AD_FIX_FORMAT_1080P_30FPS:
	case AD_FIX_FORMAT_1080P_25FPS:

	default:
		mode = CVSTD_720P25;
		break;
	}

	return mode;
}

int nvp6188_ad_init(struct vehicle_ad_dev *ad)
{
	int val;
	int i = 0;

	nvp6188_g_addev = ad;

	/*  1. i2c init */
	while (ad->adapter == NULL) {
		ad->adapter = i2c_get_adapter(ad->i2c_chl);
		usleep_range(10000, 12000);
	}
	if (ad->adapter == NULL)
		return -ENODEV;

	if (!i2c_check_functionality(ad->adapter, I2C_FUNC_I2C))
		return -EIO;

	/*  2. ad power on sequence */
	nvp6188_power_on(ad);

	while (++i < 5) {
		usleep_range(1000, 1200);
		val = vehicle_generic_sensor_read(ad, 0xf0);
		if (val != 0xff)
			break;
		VEHICLE_INFO("nvp6188_init i2c_reg_read fail\n");
	}

	nvp6188_reinit_parameter(ad, cvstd_mode);
	ad->last_detect_status = true;

	/*  create workqueue to detect signal change */
	INIT_DELAYED_WORK(&ad->state_check_work.work, nvp6188_check_state_work);
	ad->state_check_work.state_check_wq =
		create_singlethread_workqueue("vehicle-ad-nvp6188");

	queue_delayed_work(ad->state_check_work.state_check_wq,
			   &ad->state_check_work.work, msecs_to_jiffies(100));

	return 0;
}

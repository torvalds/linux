// SPDX-License-Identifier: GPL-2.0
/*
 * V4L2 driver for Sony IMX678
 *
 * Diagonal 8.86 mm (Type 1/1.8) CMOS image sensor with 8.40 M effective pixels.
 *
 * Copyright (C) 2026 Ideas On Board Oy.
 *
 * Based on Sony IMX678 driver prepared by Will Whang & Soho Enterprise Ltd.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-rect.h>
#include <media/v4l2-subdev.h>

/* Standby or streaming mode */
#define IMX678_REG_MODE_SELECT          CCI_REG8(0x3000)
#define IMX678_MODE_STANDBY             0x01
#define IMX678_MODE_STREAMING           0x00
#define IMX678_STREAM_DELAY_US          25000
#define IMX678_STREAM_DELAY_RANGE_US    1000

/* XVS/XHS sync control */
#define IMX678_REG_XMSTA                CCI_REG8(0x3002)
#define IMX678_REG_XXS_DRV              CCI_REG8(0x30a6)
#define IMX678_REG_XXS_OUTSEL           CCI_REG8(0x30a4)

/* Clk selection */
#define IMX678_REG_INCK_SEL             CCI_REG8(0x3014)

/* Link Speed */
#define IMX678_REG_DATARATE_SEL         CCI_REG8(0x3015)

/* Lane Count */
#define IMX678_REG_LANEMODE             CCI_REG8(0x3040)

/*
 * The internal readout clock runs at 74.25 MHz. In one cycle the AD reads 8
 * pixels, thus giving us a rate of 74.25 * 8 = 594 MPix/s
 */
#define IMX678_PIXEL_RATE		594000000
#define IMX678_PIX_PER_CLK		8

/* VMAX - Frame Length in Lines */
#define IMX678_REG_VMAX                 CCI_REG24_LE(0x3028)
#define IMX678_VMAX_MAX                 0xfffff
#define IMX678_VMAX_DEFAULT             2250

/* HMAX - Line Length in Cycles (8 Pixels) */
#define IMX678_REG_HMAX                 CCI_REG16_LE(0x302c)
#define IMX678_HMAX_MAX                 0xffff

/* SHR internal */
#define IMX678_REG_SHR                  CCI_REG24_LE(0x3050)
#define IMX678_SHR_MIN                  8

/* Exposure control */
#define IMX678_EXPOSURE_MIN             2
#define IMX678_EXPOSURE_STEP            1
#define IMX678_EXPOSURE_DEFAULT         1000

/*
 * Analogue gain control
 * Range is from 0 to 100 (0dB - 30dB) with 0.3dB step size
 * Values from 101 to 240 are valid but correspond to additional digital gain
 * (0.3dB - 42dB) so don't expose it to userspace
 */
#define IMX678_REG_GAIN			CCI_REG16_LE(0x3070)
#define IMX678_ANA_GAIN_MIN_NORMAL      0
#define IMX678_ANA_GAIN_MAX_NORMAL      100
#define IMX678_ANA_GAIN_STEP            1
#define IMX678_ANA_GAIN_DEFAULT         0

/* Crop */
#define IMX678_REG_WINMODE		CCI_REG8(0x3018)
#define IMX678_REG_PIX_HST		CCI_REG16_LE(0x303c)
#define IMX678_REG_PIX_HWIDTH		CCI_REG16_LE(0x303e)
#define IMX678_REG_PIX_VST		CCI_REG16_LE(0x3044)
#define IMX678_REG_PIX_VWIDTH		CCI_REG16_LE(0x3046)

/* Flip */
#define IMX678_REG_WINMODEH             CCI_REG8(0x3020)
#define IMX678_REG_WINMODEV             CCI_REG8(0x3021)

/* Sensor Identification */
#define IMX678_REG_MONOCHROME		CCI_REG8(0x4d18)
#define IMX678_TYPE			BIT(0)
#define IMX678_REG_MODULE_ID		CCI_REG16_LE(0x4d1c)
#define IMX678_ID			0x02a6
#define IMX678_MODULE_ID_DELAY		80000

/* Common configuration registers */
#define IMX678_REG_WDMODE               CCI_REG8(0x301a)
#define IMX678_REG_ADDMODE              CCI_REG8(0x301b)
#define IMX678_REG_THIN_V_EN            CCI_REG8(0x301c)
#define IMX678_REG_VCMODE               CCI_REG8(0x301e)
#define IMX678_REG_ADBIT                CCI_REG8(0x3022)
#define IMX678_REG_MDBIT                CCI_REG8(0x3023)
#define IMX678_REG_GAIN_PGC_FIDMD       CCI_REG8(0x3400)

/* Test pattern generator */
#define IMX678_REG_TPG_EN_DUOUT		CCI_REG8(0x30e0)
#define IMX678_REG_TPG_PATSEL_DUOUT	CCI_REG8(0x30e2)
#define IMX678_TPG_ALL_000		0
#define IMX678_TPG_ALL_FFF		1
#define IMX678_TPG_ALL_555		2
#define IMX678_TPG_ALL_AAA		3
#define IMX678_TPG_TOG_555_AAA		4
#define IMX678_TPG_TOG_AAA_555		5
#define IMX678_TPG_TOG_000_555		6
#define IMX678_TPG_TOG_555_000		7
#define IMX678_TPG_TOG_000_FFF		8
#define IMX678_TPG_TOG_FFF_000		9
#define IMX678_TPG_H_COLOR_BARS		10
#define IMX678_TPG_V_COLOR_BARS		11
#define IMX678_REG_TPG_COLORWIDTH	CCI_REG8(0x30e4)
#define IMX678_TPG_COLORWIDTH_80PIX	0
#define IMX678_TPG_COLORWIDTH_160PIX	1
#define IMX678_TPG_COLORWIDTH_320PIX	2
#define IMX678_TPG_COLORWIDTH_640PIX	3

#define IMX678_REG_INTERFACE_SEL	CCI_REG8(0x4e3c)
#define IMX678_INTERFACE_2L_4L		0x07
#define IMX678_INTERFACE_8L_2x4L	0x7f

/* Minimum output resolution */
#define IMX678_PIXEL_ARRAY_MIN_WIDTH	1040
#define IMX678_PIXEL_ARRAY_MIN_HEIGHT	956

/* Sensor windowing register alignment */
#define IMX678_CROP_HWIDTH_ALIGN	16
#define IMX678_CROP_VWIDTH_ALIGN	4
#define IMX678_CROP_HST_ALIGN		4
#define IMX678_CROP_VST_ALIGN		4

/* Subdev pads */
#define IMX678_SOURCE_PAD		0

/* IMX678 native and active pixel array size. */
static const struct v4l2_rect imx678_native_area = {
	.top = 0,
	.left = 0,
	.width = 3857,
	.height = 2201,
};

static const struct v4l2_rect imx678_active_area = {
	.top = 20,
	.left = 0,
	.width = 3856,
	.height = 2180,
};

enum imx678_type {
	IMX678_COLOR = 0,
	IMX678_MONOCHROME = 1,
};

struct imx678_model_info {
	enum imx678_type type;
	const u32 *codes;
	unsigned int num_codes;
};

enum imx678_lanemode {
	IMX678_LANEMODE_2L = 1,
	IMX678_LANEMODE_4L = 3,
};

/* Link frequency setup (DDR: lane rate = 2 x link freq) */
enum {
	IMX678_LINK_FREQ_297MHZ,
	IMX678_LINK_FREQ_360MHZ,
	IMX678_LINK_FREQ_445MHZ,
	IMX678_LINK_FREQ_594MHZ,
	IMX678_LINK_FREQ_720MHZ,
	IMX678_LINK_FREQ_891MHZ,
	IMX678_LINK_FREQ_1039MHZ,
	IMX678_LINK_FREQ_1188MHZ,
};

static const u8 link_freqs_reg_value[] = {
	[IMX678_LINK_FREQ_297MHZ]  = 0x07,
	[IMX678_LINK_FREQ_360MHZ]  = 0x06,
	[IMX678_LINK_FREQ_445MHZ]  = 0x05,
	[IMX678_LINK_FREQ_594MHZ]  = 0x04,
	[IMX678_LINK_FREQ_720MHZ]  = 0x03,
	[IMX678_LINK_FREQ_891MHZ]  = 0x02,
	[IMX678_LINK_FREQ_1039MHZ] = 0x01,
	[IMX678_LINK_FREQ_1188MHZ] = 0x00,
};

static const u64 link_freqs[] = {
	[IMX678_LINK_FREQ_297MHZ]  = 297000000,
	[IMX678_LINK_FREQ_360MHZ]  = 360000000,
	[IMX678_LINK_FREQ_445MHZ]  = 445500000,
	[IMX678_LINK_FREQ_594MHZ]  = 594000000,
	[IMX678_LINK_FREQ_720MHZ]  = 720000000,
	[IMX678_LINK_FREQ_891MHZ]  = 891000000,
	[IMX678_LINK_FREQ_1039MHZ] = 1039500000,
	[IMX678_LINK_FREQ_1188MHZ] = 1188000000,
};

static const u16 min_hmax_4lane[] = {
	[IMX678_LINK_FREQ_297MHZ] = 1584,
	[IMX678_LINK_FREQ_360MHZ] = 1320,
	[IMX678_LINK_FREQ_445MHZ] = 1100,
	[IMX678_LINK_FREQ_594MHZ] =  792,
	[IMX678_LINK_FREQ_720MHZ] =  660,
	[IMX678_LINK_FREQ_891MHZ] =  550,
	[IMX678_LINK_FREQ_1039MHZ] = 550,
	[IMX678_LINK_FREQ_1188MHZ] = 550,
};

struct imx678_inck_cfg {
	u32 xclk_hz;   /* platform clock rate  */
	u8  inck_sel;  /* value for reg        */
};

static const struct imx678_inck_cfg imx678_inck_table[] = {
	{ 74250000, 0x00 },
	{ 37125000, 0x01 },
	{ 72000000, 0x02 },
	{ 27000000, 0x03 },
	{ 24000000, 0x04 },
	{ 36000000, 0x05 },
	{ 18000000, 0x06 },
	{ 13500000, 0x07 },
};

static const char * const imx678_tpg_menu[] = {
	"Disabled",
	"All 000h",
	"All FFFh",
	"All 555h",
	"All AAAh",
	"Toggle 555/AAAh",
	"Toggle AAA/555h",
	"Toggle 000/555h",
	"Toggle 555/000h",
	"Toggle 000/FFFh",
	"Toggle FFF/000h",
	"Horizontal color bars",
	"Vertical color bars",
};

static const int imx678_tpg_val[] = {
	IMX678_TPG_ALL_000,
	IMX678_TPG_ALL_000,
	IMX678_TPG_ALL_FFF,
	IMX678_TPG_ALL_555,
	IMX678_TPG_ALL_AAA,
	IMX678_TPG_TOG_555_AAA,
	IMX678_TPG_TOG_AAA_555,
	IMX678_TPG_TOG_000_555,
	IMX678_TPG_TOG_555_000,
	IMX678_TPG_TOG_000_FFF,
	IMX678_TPG_TOG_FFF_000,
	IMX678_TPG_H_COLOR_BARS,
	IMX678_TPG_V_COLOR_BARS,
};

/* Common configuration */
static const struct cci_reg_sequence common_regs[] = {
	{ IMX678_REG_THIN_V_EN, 0x00 },
	{ IMX678_REG_VCMODE, 0x01 },
	{ CCI_REG8(0x306b), 0x00 },
	{ IMX678_REG_GAIN_PGC_FIDMD, 0x01 },
	{ CCI_REG8(0x3460), 0x22 },
	{ CCI_REG8(0x355a), 0x64 },
	{ CCI_REG8(0x3a02), 0x7a },
	{ CCI_REG8(0x3a10), 0xec },
	{ CCI_REG8(0x3a12), 0x71 },
	{ CCI_REG8(0x3a14), 0xde },
	{ CCI_REG8(0x3a20), 0x2b },
	{ CCI_REG8(0x3a24), 0x22 },
	{ CCI_REG8(0x3a25), 0x25 },
	{ CCI_REG8(0x3a26), 0x2a },
	{ CCI_REG8(0x3a27), 0x2c },
	{ CCI_REG8(0x3a28), 0x39 },
	{ CCI_REG8(0x3a29), 0x38 },
	{ CCI_REG8(0x3a30), 0x04 },
	{ CCI_REG8(0x3a31), 0x04 },
	{ CCI_REG8(0x3a32), 0x03 },
	{ CCI_REG8(0x3a33), 0x03 },
	{ CCI_REG8(0x3a34), 0x09 },
	{ CCI_REG8(0x3a35), 0x06 },
	{ CCI_REG8(0x3a38), 0xcd },
	{ CCI_REG8(0x3a3a), 0x4c },
	{ CCI_REG8(0x3a3c), 0xb9 },
	{ CCI_REG8(0x3a3e), 0x30 },
	{ CCI_REG8(0x3a40), 0x2c },
	{ CCI_REG8(0x3a42), 0x39 },
	{ CCI_REG8(0x3a4e), 0x00 },
	{ CCI_REG8(0x3a52), 0x00 },
	{ CCI_REG8(0x3a56), 0x00 },
	{ CCI_REG8(0x3a5a), 0x00 },
	{ CCI_REG8(0x3a5e), 0x00 },
	{ CCI_REG8(0x3a62), 0x00 },
	{ CCI_REG8(0x3a64), 0x00 },
	{ CCI_REG8(0x3a6e), 0xa0 },
	{ CCI_REG8(0x3a70), 0x50 },
	{ CCI_REG8(0x3a8c), 0x04 },
	{ CCI_REG8(0x3a8d), 0x03 },
	{ CCI_REG8(0x3a8e), 0x09 },
	{ CCI_REG8(0x3a90), 0x38 },
	{ CCI_REG8(0x3a91), 0x42 },
	{ CCI_REG8(0x3a92), 0x3c },
	{ CCI_REG8(0x3b0e), 0xf3 },
	{ CCI_REG8(0x3b12), 0xe5 },
	{ CCI_REG8(0x3b27), 0xc0 },
	{ CCI_REG8(0x3b2e), 0xef },
	{ CCI_REG8(0x3b30), 0x6a },
	{ CCI_REG8(0x3b32), 0xf6 },
	{ CCI_REG8(0x3b36), 0xe1 },
	{ CCI_REG8(0x3b3a), 0xe8 },
	{ CCI_REG8(0x3b5a), 0x17 },
	{ CCI_REG8(0x3b5e), 0xef },
	{ CCI_REG8(0x3b60), 0x6a },
	{ CCI_REG8(0x3b62), 0xf6 },
	{ CCI_REG8(0x3b66), 0xe1 },
	{ CCI_REG8(0x3b6a), 0xe8 },
	{ CCI_REG8(0x3b88), 0xec },
	{ CCI_REG8(0x3b8a), 0xed },
	{ CCI_REG8(0x3b94), 0x71 },
	{ CCI_REG8(0x3b96), 0x72 },
	{ CCI_REG8(0x3b98), 0xde },
	{ CCI_REG8(0x3b9a), 0xdf },
	{ CCI_REG8(0x3c0f), 0x06 },
	{ CCI_REG8(0x3c10), 0x06 },
	{ CCI_REG8(0x3c11), 0x06 },
	{ CCI_REG8(0x3c12), 0x06 },
	{ CCI_REG8(0x3c13), 0x06 },
	{ CCI_REG8(0x3c18), 0x20 },
	{ CCI_REG8(0x3c37), 0x10 },
	{ CCI_REG8(0x3c3a), 0x7a },
	{ CCI_REG8(0x3c40), 0xf4 },
	{ CCI_REG8(0x3c48), 0xe6 },
	{ CCI_REG8(0x3c54), 0xce },
	{ CCI_REG8(0x3c56), 0xd0 },
	{ CCI_REG8(0x3c6c), 0x53 },
	{ CCI_REG8(0x3c6e), 0x55 },
	{ CCI_REG8(0x3c70), 0xc0 },
	{ CCI_REG8(0x3c72), 0xc2 },
	{ CCI_REG8(0x3c7e), 0xce },
	{ CCI_REG8(0x3c8c), 0xcf },
	{ CCI_REG8(0x3c8e), 0xeb },
	{ CCI_REG8(0x3c98), 0x54 },
	{ CCI_REG8(0x3c9a), 0x70 },
	{ CCI_REG8(0x3c9c), 0xc1 },
	{ CCI_REG8(0x3c9e), 0xdd },
	{ CCI_REG8(0x3cb0), 0x7a },
	{ CCI_REG8(0x3cb2), 0xba },
	{ CCI_REG8(0x3cc8), 0xbc },
	{ CCI_REG8(0x3cca), 0x7c },
	{ CCI_REG8(0x3cd4), 0xea },
	{ CCI_REG8(0x3cd5), 0x01 },
	{ CCI_REG8(0x3cd6), 0x4a },
	{ CCI_REG8(0x3cd8), 0x00 },
	{ CCI_REG8(0x3cd9), 0x00 },
	{ CCI_REG8(0x3cda), 0xff },
	{ CCI_REG8(0x3cdb), 0x03 },
	{ CCI_REG8(0x3cdc), 0x00 },
	{ CCI_REG8(0x3cdd), 0x00 },
	{ CCI_REG8(0x3cde), 0xff },
	{ CCI_REG8(0x3cdf), 0x03 },
	{ CCI_REG8(0x3ce4), 0x4c },
	{ CCI_REG8(0x3ce6), 0xec },
	{ CCI_REG8(0x3ce7), 0x01 },
	{ CCI_REG8(0x3ce8), 0xff },
	{ CCI_REG8(0x3ce9), 0x03 },
	{ CCI_REG8(0x3cea), 0x00 },
	{ CCI_REG8(0x3ceb), 0x00 },
	{ CCI_REG8(0x3cec), 0xff },
	{ CCI_REG8(0x3ced), 0x03 },
	{ CCI_REG8(0x3cee), 0x00 },
	{ CCI_REG8(0x3cef), 0x00 },
	{ CCI_REG8(0x3cf2), 0xff },
	{ CCI_REG8(0x3cf3), 0x03 },
	{ CCI_REG8(0x3cf4), 0x00 },
	{ CCI_REG8(0x3e28), 0x82 },
	{ CCI_REG8(0x3e2a), 0x80 },
	{ CCI_REG8(0x3e30), 0x85 },
	{ CCI_REG8(0x3e32), 0x7d },
	{ CCI_REG8(0x3e5c), 0xce },
	{ CCI_REG8(0x3e5e), 0xd3 },
	{ CCI_REG8(0x3e70), 0x53 },
	{ CCI_REG8(0x3e72), 0x58 },
	{ CCI_REG8(0x3e74), 0xc0 },
	{ CCI_REG8(0x3e76), 0xc5 },
	{ CCI_REG8(0x3e78), 0xc0 },
	{ CCI_REG8(0x3e79), 0x01 },
	{ CCI_REG8(0x3e7a), 0xd4 },
	{ CCI_REG8(0x3e7b), 0x01 },
	{ CCI_REG8(0x3eb4), 0x0b },
	{ CCI_REG8(0x3eb5), 0x02 },
	{ CCI_REG8(0x3eb6), 0x4d },
	{ CCI_REG8(0x3eb7), 0x42 },
	{ CCI_REG8(0x3eec), 0xf3 },
	{ CCI_REG8(0x3eee), 0xe7 },
	{ CCI_REG8(0x3f01), 0x01 },
	{ CCI_REG8(0x3f24), 0x10 },
	{ CCI_REG8(0x3f28), 0x2d },
	{ CCI_REG8(0x3f2a), 0x2d },
	{ CCI_REG8(0x3f2c), 0x2d },
	{ CCI_REG8(0x3f2e), 0x2d },
	{ CCI_REG8(0x3f30), 0x23 },
	{ CCI_REG8(0x3f38), 0x2d },
	{ CCI_REG8(0x3f3a), 0x2d },
	{ CCI_REG8(0x3f3c), 0x2d },
	{ CCI_REG8(0x3f3e), 0x28 },
	{ CCI_REG8(0x3f40), 0x1e },
	{ CCI_REG8(0x3f48), 0x2d },
	{ CCI_REG8(0x3f4a), 0x2d },
	{ CCI_REG8(0x3f4c), 0x00 },
	{ CCI_REG8(0x4004), 0xe4 },
	{ CCI_REG8(0x4006), 0xff },
	{ CCI_REG8(0x4018), 0x69 },
	{ CCI_REG8(0x401a), 0x84 },
	{ CCI_REG8(0x401c), 0xd6 },
	{ CCI_REG8(0x401e), 0xf1 },
	{ CCI_REG8(0x4038), 0xde },
	{ CCI_REG8(0x403a), 0x00 },
	{ CCI_REG8(0x403b), 0x01 },
	{ CCI_REG8(0x404c), 0x63 },
	{ CCI_REG8(0x404e), 0x85 },
	{ CCI_REG8(0x4050), 0xd0 },
	{ CCI_REG8(0x4052), 0xf2 },
	{ CCI_REG8(0x4108), 0xdd },
	{ CCI_REG8(0x410a), 0xf7 },
	{ CCI_REG8(0x411c), 0x62 },
	{ CCI_REG8(0x411e), 0x7c },
	{ CCI_REG8(0x4120), 0xcf },
	{ CCI_REG8(0x4122), 0xe9 },
	{ CCI_REG8(0x4138), 0xe6 },
	{ CCI_REG8(0x413a), 0xf1 },
	{ CCI_REG8(0x414c), 0x6b },
	{ CCI_REG8(0x414e), 0x76 },
	{ CCI_REG8(0x4150), 0xd8 },
	{ CCI_REG8(0x4152), 0xe3 },
	{ CCI_REG8(0x417e), 0x03 },
	{ CCI_REG8(0x417f), 0x01 },
	{ CCI_REG8(0x4186), 0xe0 },
	{ CCI_REG8(0x4190), 0xf3 },
	{ CCI_REG8(0x4192), 0xf7 },
	{ CCI_REG8(0x419c), 0x78 },
	{ CCI_REG8(0x419e), 0x7c },
	{ CCI_REG8(0x41a0), 0xe5 },
	{ CCI_REG8(0x41a2), 0xe9 },
	{ CCI_REG8(0x41c8), 0xe2 },
	{ CCI_REG8(0x41ca), 0xfd },
	{ CCI_REG8(0x41dc), 0x67 },
	{ CCI_REG8(0x41de), 0x82 },
	{ CCI_REG8(0x41e0), 0xd4 },
	{ CCI_REG8(0x41e2), 0xef },
	{ CCI_REG8(0x4200), 0xde },
	{ CCI_REG8(0x4202), 0xda },
	{ CCI_REG8(0x4218), 0x63 },
	{ CCI_REG8(0x421a), 0x5f },
	{ CCI_REG8(0x421c), 0xd0 },
	{ CCI_REG8(0x421e), 0xcc },
	{ CCI_REG8(0x425a), 0x82 },
	{ CCI_REG8(0x425c), 0xef },
	{ CCI_REG8(0x4348), 0xfe },
	{ CCI_REG8(0x4349), 0x06 },
	{ CCI_REG8(0x4352), 0xce },
	{ CCI_REG8(0x4420), 0x0b },
	{ CCI_REG8(0x4421), 0x02 },
	{ CCI_REG8(0x4422), 0x4d },
	{ CCI_REG8(0x4423), 0x0a },
	{ CCI_REG8(0x4426), 0xf5 },
	{ CCI_REG8(0x442a), 0xe7 },
	{ CCI_REG8(0x4432), 0xf5 },
	{ CCI_REG8(0x4436), 0xe7 },
	{ CCI_REG8(0x4466), 0xb4 },
	{ CCI_REG8(0x446e), 0x32 },
	{ CCI_REG8(0x449f), 0x1c },
	{ CCI_REG8(0x44a4), 0x2c },
	{ CCI_REG8(0x44a6), 0x2c },
	{ CCI_REG8(0x44a8), 0x2c },
	{ CCI_REG8(0x44aa), 0x2c },
	{ CCI_REG8(0x44b4), 0x2c },
	{ CCI_REG8(0x44b6), 0x2c },
	{ CCI_REG8(0x44b8), 0x2c },
	{ CCI_REG8(0x44ba), 0x2c },
	{ CCI_REG8(0x44c4), 0x2c },
	{ CCI_REG8(0x44c6), 0x2c },
	{ CCI_REG8(0x44c8), 0x2c },
	{ CCI_REG8(0x4506), 0xf3 },
	{ CCI_REG8(0x450e), 0xe5 },
	{ CCI_REG8(0x4516), 0xf3 },
	{ CCI_REG8(0x4522), 0xe5 },
	{ CCI_REG8(0x4524), 0xf3 },
	{ CCI_REG8(0x452c), 0xe5 },
	{ CCI_REG8(0x453c), 0x22 },
	{ CCI_REG8(0x453d), 0x1b },
	{ CCI_REG8(0x453e), 0x1b },
	{ CCI_REG8(0x453f), 0x15 },
	{ CCI_REG8(0x4540), 0x15 },
	{ CCI_REG8(0x4541), 0x15 },
	{ CCI_REG8(0x4542), 0x15 },
	{ CCI_REG8(0x4543), 0x15 },
	{ CCI_REG8(0x4544), 0x15 },
	{ CCI_REG8(0x4548), 0x00 },
	{ CCI_REG8(0x4549), 0x01 },
	{ CCI_REG8(0x454a), 0x01 },
	{ CCI_REG8(0x454b), 0x06 },
	{ CCI_REG8(0x454c), 0x06 },
	{ CCI_REG8(0x454d), 0x06 },
	{ CCI_REG8(0x454e), 0x06 },
	{ CCI_REG8(0x454f), 0x06 },
	{ CCI_REG8(0x4550), 0x06 },
	{ CCI_REG8(0x4554), 0x55 },
	{ CCI_REG8(0x4555), 0x02 },
	{ CCI_REG8(0x4556), 0x42 },
	{ CCI_REG8(0x4557), 0x05 },
	{ CCI_REG8(0x4558), 0xfd },
	{ CCI_REG8(0x4559), 0x05 },
	{ CCI_REG8(0x455a), 0x94 },
	{ CCI_REG8(0x455b), 0x06 },
	{ CCI_REG8(0x455d), 0x06 },
	{ CCI_REG8(0x455e), 0x49 },
	{ CCI_REG8(0x455f), 0x07 },
	{ CCI_REG8(0x4560), 0x7f },
	{ CCI_REG8(0x4561), 0x07 },
	{ CCI_REG8(0x4562), 0xa5 },
	{ CCI_REG8(0x4564), 0x55 },
	{ CCI_REG8(0x4565), 0x02 },
	{ CCI_REG8(0x4566), 0x42 },
	{ CCI_REG8(0x4567), 0x05 },
	{ CCI_REG8(0x4568), 0xfd },
	{ CCI_REG8(0x4569), 0x05 },
	{ CCI_REG8(0x456a), 0x94 },
	{ CCI_REG8(0x456b), 0x06 },
	{ CCI_REG8(0x456d), 0x06 },
	{ CCI_REG8(0x456e), 0x49 },
	{ CCI_REG8(0x456f), 0x07 },
	{ CCI_REG8(0x4572), 0xa5 },
	{ CCI_REG8(0x460c), 0x7d },
	{ CCI_REG8(0x460e), 0xb1 },
	{ CCI_REG8(0x4614), 0xa8 },
	{ CCI_REG8(0x4616), 0xb2 },
	{ CCI_REG8(0x461c), 0x7e },
	{ CCI_REG8(0x461e), 0xa7 },
	{ CCI_REG8(0x4624), 0xa8 },
	{ CCI_REG8(0x4626), 0xb2 },
	{ CCI_REG8(0x462c), 0x7e },
	{ CCI_REG8(0x462e), 0x8a },
	{ CCI_REG8(0x4630), 0x94 },
	{ CCI_REG8(0x4632), 0xa7 },
	{ CCI_REG8(0x4634), 0xfb },
	{ CCI_REG8(0x4636), 0x2f },
	{ CCI_REG8(0x4638), 0x81 },
	{ CCI_REG8(0x4639), 0x01 },
	{ CCI_REG8(0x463a), 0xb5 },
	{ CCI_REG8(0x463b), 0x01 },
	{ CCI_REG8(0x463c), 0x26 },
	{ CCI_REG8(0x463e), 0x30 },
	{ CCI_REG8(0x4640), 0xac },
	{ CCI_REG8(0x4641), 0x01 },
	{ CCI_REG8(0x4642), 0xb6 },
	{ CCI_REG8(0x4643), 0x01 },
	{ CCI_REG8(0x4644), 0xfc },
	{ CCI_REG8(0x4646), 0x25 },
	{ CCI_REG8(0x4648), 0x82 },
	{ CCI_REG8(0x4649), 0x01 },
	{ CCI_REG8(0x464a), 0xab },
	{ CCI_REG8(0x464b), 0x01 },
	{ CCI_REG8(0x464c), 0x26 },
	{ CCI_REG8(0x464e), 0x30 },
	{ CCI_REG8(0x4654), 0xfc },
	{ CCI_REG8(0x4656), 0x08 },
	{ CCI_REG8(0x4658), 0x12 },
	{ CCI_REG8(0x465a), 0x25 },
	{ CCI_REG8(0x4662), 0xfc },
	{ CCI_REG8(0x46a2), 0xfb },
	{ CCI_REG8(0x46d6), 0xf3 },
	{ CCI_REG8(0x46e6), 0x00 },
	{ CCI_REG8(0x46e8), 0xff },
	{ CCI_REG8(0x46e9), 0x03 },
	{ CCI_REG8(0x46ec), 0x7a },
	{ CCI_REG8(0x46ee), 0xe5 },
	{ CCI_REG8(0x46f4), 0xee },
	{ CCI_REG8(0x46f6), 0xf2 },
	{ CCI_REG8(0x470c), 0xff },
	{ CCI_REG8(0x470d), 0x03 },
	{ CCI_REG8(0x470e), 0x00 },
	{ CCI_REG8(0x4714), 0xe0 },
	{ CCI_REG8(0x4716), 0xe4 },
	{ CCI_REG8(0x471e), 0xed },
	{ CCI_REG8(0x472e), 0x00 },
	{ CCI_REG8(0x4730), 0xff },
	{ CCI_REG8(0x4731), 0x03 },
	{ CCI_REG8(0x4734), 0x7b },
	{ CCI_REG8(0x4736), 0xdf },
	{ CCI_REG8(0x4754), 0x7d },
	{ CCI_REG8(0x4756), 0x8b },
	{ CCI_REG8(0x4758), 0x93 },
	{ CCI_REG8(0x475a), 0xb1 },
	{ CCI_REG8(0x475c), 0xfb },
	{ CCI_REG8(0x475e), 0x09 },
	{ CCI_REG8(0x4760), 0x11 },
	{ CCI_REG8(0x4762), 0x2f },
	{ CCI_REG8(0x4766), 0xcc },
	{ CCI_REG8(0x4776), 0xcb },
	{ CCI_REG8(0x477e), 0x4a },
	{ CCI_REG8(0x478e), 0x49 },
	{ CCI_REG8(0x4794), 0x7c },
	{ CCI_REG8(0x4796), 0x8f },
	{ CCI_REG8(0x4798), 0xb3 },
	{ CCI_REG8(0x4799), 0x00 },
	{ CCI_REG8(0x479a), 0xcc },
	{ CCI_REG8(0x479c), 0xc1 },
	{ CCI_REG8(0x479e), 0xcb },
	{ CCI_REG8(0x47a4), 0x7d },
	{ CCI_REG8(0x47a6), 0x8e },
	{ CCI_REG8(0x47a8), 0xb4 },
	{ CCI_REG8(0x47a9), 0x00 },
	{ CCI_REG8(0x47aa), 0xc0 },
	{ CCI_REG8(0x47ac), 0xfa },
	{ CCI_REG8(0x47ae), 0x0d },
	{ CCI_REG8(0x47b0), 0x31 },
	{ CCI_REG8(0x47b1), 0x01 },
	{ CCI_REG8(0x47b2), 0x4a },
	{ CCI_REG8(0x47b3), 0x01 },
	{ CCI_REG8(0x47b4), 0x3f },
	{ CCI_REG8(0x47b6), 0x49 },
	{ CCI_REG8(0x47bc), 0xfb },
	{ CCI_REG8(0x47be), 0x0c },
	{ CCI_REG8(0x47c0), 0x32 },
	{ CCI_REG8(0x47c1), 0x01 },
	{ CCI_REG8(0x47c2), 0x3e },
	{ CCI_REG8(0x47c3), 0x01 },
	{ IMX678_REG_WDMODE, 0x00 },
	{ IMX678_REG_MDBIT, 0x01 },
	{ IMX678_REG_XXS_DRV, 0x00 },
};

static const u32 codes_bayer[] = {
	MEDIA_BUS_FMT_SRGGB12_1X12,
};

static const u32 codes_monochrome[] = {
	MEDIA_BUS_FMT_Y12_1X12,
};

static const struct imx678_model_info imx678_aaqr_info = {
	.type = IMX678_COLOR,
	.codes = codes_bayer,
	.num_codes = ARRAY_SIZE(codes_bayer),
};

static const struct imx678_model_info imx678_aamr_info = {
	.type = IMX678_MONOCHROME,
	.codes = codes_monochrome,
	.num_codes = ARRAY_SIZE(codes_monochrome),
};

static const char * const imx678_supply_name[] = {
	"avdd",  /* Analog (3.3V) supply */
	"dvdd",  /* Digital Core (1.1V) supply */
	"ovdd",  /* IF (1.8V) supply */
};

struct imx678 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct regmap *cci;

	const struct imx678_model_info *info;

	struct clk *xclk;
	u32 xclk_freq;

	/* chosen INCK_SEL register value */
	u8 inck_sel_val;

	/* Link configurations */
	enum imx678_lanemode lane_mode;
	unsigned long link_freq_bitmap;

	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[ARRAY_SIZE(imx678_supply_name)];

	struct v4l2_ctrl_handler ctrl_handler;

	/* V4L2 Controls */
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;

	/* Track VMAX for exposure updates */
	u32 vmax;
};

static inline struct imx678 *to_imx678(struct v4l2_subdev *_sd)
{
	return container_of_const(_sd, struct imx678, sd);
}

static u32 imx678_default_mbus_code(struct imx678 *imx678)
{
	return imx678->info->codes[0];
}

static bool imx678_mbus_code_supported(struct imx678 *imx678, u32 code)
{
	for (unsigned int i = 0; i < imx678->info->num_codes; i++) {
		if (imx678->info->codes[i] == code)
			return true;
	}

	return false;
}

static int imx678_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx678 *imx678 = container_of_const(ctrl->handler, struct
						   imx678, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&imx678->sd);
	const struct v4l2_mbus_framefmt *format;
	struct v4l2_subdev_state *state;
	int ret = 0;

	state = v4l2_subdev_get_locked_active_state(&imx678->sd);
	format = v4l2_subdev_state_get_format(state, IMX678_SOURCE_PAD);

	if (ctrl->id == V4L2_CID_VBLANK) {
		u32 current_exposure = imx678->exposure->cur.val;

		imx678->vmax = format->height + ctrl->val;

		current_exposure = clamp_t(u32, current_exposure,
					   IMX678_EXPOSURE_MIN,
					   imx678->vmax - IMX678_SHR_MIN);
		ret = __v4l2_ctrl_modify_range(imx678->exposure,
					       IMX678_EXPOSURE_MIN,
					       imx678->vmax - IMX678_SHR_MIN,
					       1, current_exposure);
		if (ret)
			return ret;
	}

	/*
	 * Only apply control values when device is powered on (RPM ACTIVE)
	 * and streaming (usage count != 0)
	 */
	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		cci_write(imx678->cci, IMX678_REG_VMAX, imx678->vmax, &ret);
		fallthrough; /* SHR = VMAX - exposure, so update it */
	case V4L2_CID_EXPOSURE: {
		u32 shr = imx678->vmax - imx678->exposure->val;

		cci_write(imx678->cci, IMX678_REG_SHR, shr, &ret);
		break;
	}
	case V4L2_CID_ANALOGUE_GAIN:
		cci_write(imx678->cci, IMX678_REG_GAIN, ctrl->val, &ret);
		break;
	case V4L2_CID_HBLANK: {
		u32 hmax = (format->width + ctrl->val) / IMX678_PIX_PER_CLK;

		cci_write(imx678->cci, IMX678_REG_HMAX, hmax, &ret);
		break;
	}
	case V4L2_CID_TEST_PATTERN: {
		cci_write(imx678->cci, IMX678_REG_TPG_COLORWIDTH,
			  IMX678_TPG_COLORWIDTH_160PIX, &ret);
		cci_write(imx678->cci, IMX678_REG_TPG_PATSEL_DUOUT,
			  imx678_tpg_val[ctrl->val], &ret);
		cci_write(imx678->cci, IMX678_REG_TPG_EN_DUOUT,
			  (ctrl->val) ? 1 : 0,
			  &ret);
		break;
	}
	case V4L2_CID_HFLIP:
		cci_write(imx678->cci, IMX678_REG_WINMODEH, ctrl->val, &ret);
		break;
	case V4L2_CID_VFLIP:
		cci_write(imx678->cci, IMX678_REG_WINMODEV, ctrl->val, &ret);
		break;
	default:
		dev_warn(&client->dev,
			 "ctrl(id:0x%x,val:0x%x) is not handled\n",
			 ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx678_ctrl_ops = {
	.s_ctrl = imx678_set_ctrl,
};

static int imx678_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx678 *imx678 = to_imx678(sd);

	if (code->index >= imx678->info->num_codes)
		return -EINVAL;

	code->code = imx678->info->codes[code->index];

	return 0;
}

static int imx678_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct imx678 *imx678 = to_imx678(sd);
	const struct v4l2_rect *crop;

	if (fse->index)
		return -EINVAL;

	if (!imx678_mbus_code_supported(imx678, fse->code))
		return -EINVAL;

	crop = v4l2_subdev_state_get_crop(sd_state, fse->pad);

	fse->min_width = crop->width;
	fse->max_width = fse->min_width;
	fse->min_height = crop->height;
	fse->max_height = fse->min_height;

	return 0;
}

static int imx678_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r = *v4l2_subdev_state_get_crop(sd_state, sel->pad);
		return 0;

	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r = imx678_native_area;
		return 0;

	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r = imx678_active_area;
		return 0;
	}

	return -EINVAL;
}

static int imx678_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state)
{
	struct imx678 *imx678 = to_imx678(sd);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *crop;

	crop = v4l2_subdev_state_get_crop(state, IMX678_SOURCE_PAD);
	*crop = imx678_active_area;

	format = v4l2_subdev_state_get_format(state, IMX678_SOURCE_PAD);
	format->code = imx678_default_mbus_code(imx678);
	format->width = imx678_active_area.width;
	format->height = imx678_active_area.height;
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_RAW;
	format->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	format->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	format->xfer_func = V4L2_XFER_FUNC_NONE;

	return 0;
}

static int imx678_write_common(struct imx678 *imx678)
{
	int ret = 0;

	cci_multi_reg_write(imx678->cci, common_regs, ARRAY_SIZE(common_regs),
			    &ret);

	cci_write(imx678->cci, IMX678_REG_INCK_SEL, imx678->inck_sel_val, &ret);
	cci_write(imx678->cci, IMX678_REG_DATARATE_SEL,
		  link_freqs_reg_value[__ffs(imx678->link_freq_bitmap)], &ret);
	cci_write(imx678->cci, IMX678_REG_LANEMODE, imx678->lane_mode, &ret);

	cci_write(imx678->cci, IMX678_REG_INTERFACE_SEL, IMX678_INTERFACE_2L_4L,
		  &ret);

	return ret;
}

static int imx678_program_window(struct imx678 *imx678,
				 const struct v4l2_rect *crop)
{
	int ret = 0;

	cci_write(imx678->cci, IMX678_REG_ADDMODE, 0x00, &ret);
	cci_write(imx678->cci, IMX678_REG_WINMODE,
		  v4l2_rect_equal(crop, &imx678_active_area) ? 0x00 : 0x04,
		  &ret);
	cci_write(imx678->cci, IMX678_REG_PIX_HST,
		  crop->left - imx678_active_area.left, &ret);
	cci_write(imx678->cci, IMX678_REG_PIX_HWIDTH, crop->width, &ret);
	cci_write(imx678->cci, IMX678_REG_PIX_VST,
		  crop->top - imx678_active_area.top, &ret);
	cci_write(imx678->cci, IMX678_REG_PIX_VWIDTH, crop->height, &ret);
	cci_write(imx678->cci, IMX678_REG_ADBIT, 0x01, &ret);

	return ret;
}

static int imx678_enable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state, u32 pad,
				 u64 mask)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct imx678 *imx678 = to_imx678(sd);
	const struct v4l2_rect *crop;
	int ret;

	ret = pm_runtime_resume_and_get(&client->dev);
	if (ret < 0)
		return ret;

	crop = v4l2_subdev_state_get_crop(state, pad);
	ret = imx678_program_window(imx678, crop);
	if (ret) {
		dev_err(&client->dev, "%s failed to set mode\n", __func__);
		goto err_rpm_put;
	}

	ret = __v4l2_ctrl_handler_setup(imx678->sd.ctrl_handler);
	if (ret) {
		dev_err(&client->dev, "%s failed to apply user values\n",
			__func__);
		goto err_rpm_put;
	}

	cci_write(imx678->cci, IMX678_REG_MODE_SELECT, IMX678_MODE_STREAMING,
		  &ret);
	usleep_range(IMX678_STREAM_DELAY_US, IMX678_STREAM_DELAY_US +
		     IMX678_STREAM_DELAY_RANGE_US);
	cci_write(imx678->cci, IMX678_REG_XMSTA, 0x00, &ret);

	if (ret) {
		dev_err(&client->dev, "%s failed to start streaming\n",
			__func__);
		goto err_rpm_put;
	}

	return 0;

err_rpm_put:
	pm_runtime_put(&client->dev);

	return ret;
}

static int imx678_disable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  u32 pad, u64 mask)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct imx678 *imx678 = to_imx678(sd);
	int ret = 0;

	/* Master mode disable */
	cci_write(imx678->cci, IMX678_REG_XMSTA, 0x01, &ret);
	/* Standby */
	cci_write(imx678->cci, IMX678_REG_MODE_SELECT, IMX678_MODE_STANDBY,
		  &ret);
	if (ret)
		dev_err(&client->dev, "%s failed to stop stream\n", __func__);

	pm_runtime_put(&client->dev);

	return ret;
}

static int imx678_power_on(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx678 *imx678 = to_imx678(sd);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(imx678_supply_name),
				    imx678->supplies);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable regulators\n",
			__func__);
		return ret;
	}

	fsleep(1); /* Tlow > 500ns */

	gpiod_set_value_cansleep(imx678->reset_gpio, 0);

	fsleep(1); /* T3 > 1us */

	ret = clk_prepare_enable(imx678->xclk);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable clock\n",
			__func__);
		goto reg_off;
	}

	fsleep(20); /* T4 > 20us */

	ret = imx678_write_common(imx678);
	if (ret) {
		dev_err(&client->dev, "%s failed to write registers\n",
			__func__);
		goto clk_off;
	}

	return 0;

clk_off:
	clk_disable_unprepare(imx678->xclk);

reg_off:
	gpiod_set_value_cansleep(imx678->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(imx678_supply_name),
			       imx678->supplies);

	return ret;
}

static int imx678_power_off(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx678 *imx678 = to_imx678(sd);

	clk_disable_unprepare(imx678->xclk);
	gpiod_set_value_cansleep(imx678->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(imx678_supply_name),
			       imx678->supplies);

	return 0;
}

static int imx678_identify_model(struct imx678 *imx678)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx678->sd);
	const struct imx678_model_info *info;
	enum imx678_type detected;
	int ret = 0;
	u64 val = 0;

	info = device_get_match_data(&client->dev);

	/*
	 * This sensor's ID registers become accessible 80ms after coming out
	 * of STANDBY mode.
	 */
	cci_write(imx678->cci, IMX678_REG_MODE_SELECT, 0, &ret);
	fsleep(IMX678_MODULE_ID_DELAY);

	cci_read(imx678->cci, IMX678_REG_MODULE_ID, &val, &ret);

	if (ret) {
		dev_err(&client->dev,
			"I2C transaction failed ret = %d\n", ret);
		return ret;
	}

	if (val != IMX678_ID) {
		dev_err(&client->dev,
			"Chip ID mismatch: %x!=%llx\n", IMX678_ID, val);
		return -ENXIO;
	}

	cci_read(imx678->cci, IMX678_REG_MONOCHROME, &val, &ret);

	if (ret) {
		dev_err(&client->dev,
			"I2C transaction failed ret = %d\n", ret);
		return ret;
	}

	detected = val & IMX678_TYPE;

	/* Prefer to use sensor type specified in device tree */
	if (info) {
		imx678->info = info;
		if (detected != info->type)
			dev_err(&client->dev,
				"detected %s sensor, DT specifies %s; using DT value\n",
				detected == IMX678_COLOR ? "color" : "mono",
				info->type == IMX678_COLOR ? "color" : "mono");
	} else {
		imx678->info = detected == IMX678_MONOCHROME ?
			       &imx678_aamr_info : &imx678_aaqr_info;
		dev_info(&client->dev,
			 "sensor type missing in DT; detected %s sensor\n",
			 detected == IMX678_MONOCHROME ? "mono" : "color");
	}

	return 0;
}

static const struct v4l2_subdev_video_ops imx678_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops imx678_pad_ops = {
	.enum_mbus_code = imx678_enum_mbus_code,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = v4l2_subdev_get_fmt,
	.get_selection = imx678_get_selection,
	.enum_frame_size = imx678_enum_frame_size,
	.enable_streams = imx678_enable_streams,
	.disable_streams = imx678_disable_streams,
};

static const struct v4l2_subdev_ops imx678_subdev_ops = {
	.video = &imx678_video_ops,
	.pad = &imx678_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx678_internal_ops = {
	.init_state = imx678_init_state,
};

static int imx678_init_controls(struct imx678 *imx678)
{
	struct v4l2_ctrl_handler *ctrl_hdlr;
	const u32 hmax_4lane = min_hmax_4lane[__ffs(imx678->link_freq_bitmap)];
	const u32 lane_scale = imx678->lane_mode == IMX678_LANEMODE_2L ? 2 : 1;
	struct i2c_client *client = v4l2_get_subdevdata(&imx678->sd);
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl *link_freq;
	s32 hblank, max_hblank, vblank, max_vblank;
	u32 hmax;
	int ret;

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret < 0)
		return ret;

	ctrl_hdlr = &imx678->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 11);
	if (ret)
		return ret;

	imx678->vmax = IMX678_VMAX_DEFAULT;
	hmax = hmax_4lane * lane_scale;

	/* PIXEL_RATE is fixed and read-only */
	v4l2_ctrl_new_std(ctrl_hdlr, &imx678_ctrl_ops, V4L2_CID_PIXEL_RATE,
			  IMX678_PIXEL_RATE, IMX678_PIXEL_RATE, 1,
			  IMX678_PIXEL_RATE);

	/* LINK_FREQ is also read only */
	link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr, &imx678_ctrl_ops,
					   V4L2_CID_LINK_FREQ,
					   ARRAY_SIZE(link_freqs) - 1,
					   __ffs(imx678->link_freq_bitmap),
					   link_freqs);

	if (link_freq)
		link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank = imx678->vmax - imx678_active_area.height;
	max_vblank = IMX678_VMAX_MAX - imx678_active_area.height;
	imx678->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx678_ctrl_ops,
					   V4L2_CID_VBLANK, vblank, max_vblank,
					   2, vblank);

	hblank = hmax * IMX678_PIX_PER_CLK - imx678_active_area.width;
	max_hblank = IMX678_HMAX_MAX * IMX678_PIX_PER_CLK -
		     imx678_active_area.width;
	imx678->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx678_ctrl_ops,
					   V4L2_CID_HBLANK, hblank, max_hblank,
					   IMX678_PIX_PER_CLK, hblank);

	imx678->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &imx678_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX678_EXPOSURE_MIN,
					     IMX678_VMAX_DEFAULT -
					     IMX678_SHR_MIN,
					     IMX678_EXPOSURE_STEP,
					     IMX678_EXPOSURE_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx678_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  IMX678_ANA_GAIN_MIN_NORMAL,
			  IMX678_ANA_GAIN_MAX_NORMAL, IMX678_ANA_GAIN_STEP,
			  IMX678_ANA_GAIN_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx678_ctrl_ops, V4L2_CID_HFLIP,
			  0, 1, 1, 0);
	v4l2_ctrl_new_std(ctrl_hdlr, &imx678_ctrl_ops, V4L2_CID_VFLIP,
			  0, 1, 1, 0);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &imx678_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(imx678_tpg_menu) - 1, 0, 0,
				     imx678_tpg_menu);

	v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &imx678_ctrl_ops, &props);

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(&client->dev, "%s control init failed (%d)\n",
			__func__, ret);
		v4l2_ctrl_handler_free(ctrl_hdlr);
		return ret;
	}

	imx678->sd.ctrl_handler = ctrl_hdlr;

	return 0;
}

static int imx678_check_hwcfg(struct device *dev, struct imx678 *imx678)
{
	struct fwnode_handle *endpoint;
	struct v4l2_fwnode_endpoint ep_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	int ret = -EINVAL;

	endpoint = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev), 0, 0, 0);
	if (!endpoint) {
		dev_err(dev, "endpoint node not found\n");
		return -EINVAL;
	}

	if (v4l2_fwnode_endpoint_alloc_parse(endpoint, &ep_cfg)) {
		dev_err(dev, "could not parse endpoint\n");
		goto error_out;
	}

	switch (ep_cfg.bus.mipi_csi2.num_data_lanes) {
	case 2:
		imx678->lane_mode = IMX678_LANEMODE_2L;
		break;
	case 4:
		imx678->lane_mode = IMX678_LANEMODE_4L;
		break;
	default:
		dev_err(dev,
			"only 2 or 4 CSI2 data lanes are currently supported\n");
		goto error_out;
	}

	ret = v4l2_link_freq_to_bitmap(dev, ep_cfg.link_frequencies,
				       ep_cfg.nr_of_link_frequencies,
				       link_freqs, ARRAY_SIZE(link_freqs),
				       &imx678->link_freq_bitmap);

error_out:
	v4l2_fwnode_endpoint_free(&ep_cfg);
	fwnode_handle_put(endpoint);

	return ret;
}

static int imx678_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct imx678 *imx678;
	int ret, i;

	imx678 = devm_kzalloc(&client->dev, sizeof(*imx678), GFP_KERNEL);
	if (!imx678)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&imx678->sd, client, &imx678_subdev_ops);

	imx678->cci = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(imx678->cci))
		return dev_err_probe(dev, PTR_ERR(imx678->cci),
				     "failed to init CCI\n");

	if (imx678_check_hwcfg(dev, imx678))
		return -EINVAL;

	imx678->xclk = devm_v4l2_sensor_clk_get(dev, NULL);
	if (IS_ERR(imx678->xclk))
		return dev_err_probe(dev, PTR_ERR(imx678->xclk),
				     "failed to get xclk\n");

	imx678->xclk_freq = clk_get_rate(imx678->xclk);

	for (i = 0; i < ARRAY_SIZE(imx678_inck_table); ++i) {
		if (imx678_inck_table[i].xclk_hz == imx678->xclk_freq) {
			imx678->inck_sel_val = imx678_inck_table[i].inck_sel;
			break;
		}
	}

	if (i == ARRAY_SIZE(imx678_inck_table))
		return dev_err_probe(dev, -EINVAL,
				     "unsupported XCLK rate %u Hz\n",
				     imx678->xclk_freq);

	for (i = 0; i < ARRAY_SIZE(imx678_supply_name); i++)
		imx678->supplies[i].supply = imx678_supply_name[i];

	ret = devm_regulator_bulk_get(&client->dev,
				      ARRAY_SIZE(imx678_supply_name),
				      imx678->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get regulators\n");

	imx678->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(imx678->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(imx678->reset_gpio),
				     "failed to get reset GPIO\n");

	ret = imx678_power_on(dev);
	if (ret)
		return ret;

	ret = imx678_identify_model(imx678);
	if (ret)
		goto error_power_off;

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	ret = imx678_init_controls(imx678);
	if (ret)
		goto error_pm_runtime;

	imx678->sd.internal_ops = &imx678_internal_ops;
	imx678->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	imx678->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	imx678->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&imx678->sd.entity, 1, &imx678->pad);
	if (ret) {
		dev_err_probe(dev, ret, "failed to init entity pads\n");
		goto error_handler_free;
	}

	imx678->sd.state_lock = imx678->ctrl_handler.lock;
	ret = v4l2_subdev_init_finalize(&imx678->sd);
	if (ret < 0) {
		dev_err_probe(dev, ret, "subdev init error\n");
		goto error_media_entity;
	}

	ret = v4l2_async_register_subdev_sensor(&imx678->sd);
	if (ret < 0) {
		dev_err_probe(dev, ret,
			      "failed to register sensor sub-device\n");
		goto error_subdev_cleanup;
	}

	pm_runtime_idle(dev);

	return 0;

error_subdev_cleanup:
	v4l2_subdev_cleanup(&imx678->sd);

error_media_entity:
	media_entity_cleanup(&imx678->sd.entity);

error_handler_free:
	v4l2_ctrl_handler_free(imx678->sd.ctrl_handler);

error_pm_runtime:
	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);

error_power_off:
	imx678_power_off(&client->dev);

	return ret;
}

static void imx678_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx678 *imx678 = to_imx678(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(imx678->sd.ctrl_handler);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		imx678_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);
}

static const struct dev_pm_ops imx678_pm_ops = {
	SET_RUNTIME_PM_OPS(imx678_power_off, imx678_power_on, NULL)
};

static const struct of_device_id imx678_of_match[] = {
	{ .compatible = "sony,imx678-aamr", .data = &imx678_aamr_info },
	{ .compatible = "sony,imx678-aaqr", .data = &imx678_aaqr_info },
	/* for non-conforming DTs that rely on runtime check */
	{ .compatible = "sony,imx678" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, imx678_of_match);

static struct i2c_driver imx678_i2c_driver = {
	.driver = {
		.name = "imx678",
		.of_match_table = imx678_of_match,
		.pm = pm_ptr(&imx678_pm_ops),
	},
	.probe = imx678_probe,
	.remove = imx678_remove,
};

module_i2c_driver(imx678_i2c_driver);

MODULE_AUTHOR("Will Whang <will@willwhang.com>");
MODULE_AUTHOR("Tetsuya NOMURA <tetsuya.nomura@soho-enterprise.com>");
MODULE_AUTHOR("Jai Luthra <jai.luthra@ideasonboard.com>");
MODULE_DESCRIPTION("Sony imx678 sensor driver");
MODULE_LICENSE("GPL");

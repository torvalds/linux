// SPDX-License-Identifier: GPL-2.0
/*
 * V4L2 Support for the IMX283
 *
 * Diagonal 15.86 mm (Type 1) CMOS Image Sensor with Square Pixel for Color
 * Cameras.
 *
 * Copyright (C) 2024 Ideas on Board Oy.
 *
 * Based on Sony IMX283 driver prepared by Will Whang
 *
 * Based on Sony imx477 camera driver
 * Copyright (C) 2019-2020 Raspberry Pi (Trading) Ltd
 */

#include <linux/array_size.h>
#include <linux/bitops.h>
#include <linux/container_of.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>
#include <linux/units.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>

/* Chip ID */
#define IMX283_REG_CHIP_ID		CCI_REG8(0x3000)
#define IMX283_CHIP_ID			0x0b	// Default power on state

#define IMX283_REG_STANDBY		CCI_REG8(0x3000)
#define   IMX283_ACTIVE			0
#define   IMX283_STANDBY		BIT(0)
#define   IMX283_STBLOGIC		BIT(1)
#define   IMX283_STBMIPI		BIT(2)
#define   IMX283_STBDV			BIT(3)
#define   IMX283_SLEEP			BIT(4)

#define IMX283_REG_CLAMP		CCI_REG8(0x3001)
#define   IMX283_CLPSQRST		BIT(4)

#define IMX283_REG_PLSTMG08		CCI_REG8(0x3003)
#define   IMX283_PLSTMG08_VAL		0x77

#define IMX283_REG_MDSEL1		CCI_REG8(0x3004)
#define IMX283_REG_MDSEL2		CCI_REG8(0x3005)
#define IMX283_REG_MDSEL3		CCI_REG8(0x3006)
#define   IMX283_MDSEL3_VCROP_EN	BIT(5)
#define IMX283_REG_MDSEL4		CCI_REG8(0x3007)
#define   IMX283_MDSEL4_VCROP_EN	(BIT(4) | BIT(6))

#define IMX283_REG_SVR			CCI_REG16_LE(0x3009)

#define IMX283_REG_HTRIMMING		CCI_REG8(0x300b)
#define   IMX283_MDVREV			BIT(0) /* VFLIP */
#define   IMX283_HTRIMMING_EN		BIT(4)

#define IMX283_REG_VWINPOS		CCI_REG16_LE(0x300f)
#define IMX283_REG_VWIDCUT		CCI_REG16_LE(0x3011)

#define IMX283_REG_MDSEL7		CCI_REG16_LE(0x3013)

/* CSI Clock Configuration */
#define IMX283_REG_TCLKPOST		CCI_REG8(0x3018)
#define IMX283_REG_THSPREPARE		CCI_REG8(0x301a)
#define IMX283_REG_THSZERO		CCI_REG8(0x301c)
#define IMX283_REG_THSTRAIL		CCI_REG8(0x301e)
#define IMX283_REG_TCLKTRAIL		CCI_REG8(0x3020)
#define IMX283_REG_TCLKPREPARE		CCI_REG8(0x3022)
#define IMX283_REG_TCLKZERO		CCI_REG16_LE(0x3024)
#define IMX283_REG_TLPX			CCI_REG8(0x3026)
#define IMX283_REG_THSEXIT		CCI_REG8(0x3028)
#define IMX283_REG_TCLKPRE		CCI_REG8(0x302a)
#define IMX283_REG_SYSMODE		CCI_REG8(0x3104)

#define IMX283_REG_Y_OUT_SIZE		CCI_REG16_LE(0x302f)
#define IMX283_REG_WRITE_VSIZE		CCI_REG16_LE(0x3031)
#define IMX283_REG_OB_SIZE_V		CCI_REG8(0x3033)

/* HMAX internal HBLANK */
#define IMX283_REG_HMAX			CCI_REG16_LE(0x3036)
#define IMX283_HMAX_MAX			(BIT(16) - 1)

/* VMAX internal VBLANK */
#define IMX283_REG_VMAX			CCI_REG24_LE(0x3038)
#define   IMX283_VMAX_MAX		(BIT(16) - 1)

/* SHR internal */
#define IMX283_REG_SHR			CCI_REG16_LE(0x303b)
#define   IMX283_SHR_MIN		11

/*
 * Analog gain control
 *  Gain [dB] = -20log{(2048 - value [10:0]) /2048}
 *  Range: 0dB to approximately +27dB
 */
#define IMX283_REG_ANALOG_GAIN		CCI_REG16_LE(0x3042)
#define   IMX283_ANA_GAIN_MIN		0
#define   IMX283_ANA_GAIN_MAX		1957
#define   IMX283_ANA_GAIN_STEP		1
#define   IMX283_ANA_GAIN_DEFAULT	0x0

/*
 * Digital gain control
 *  Gain [dB] = value * 6
 *  Range: 0dB to +18db
 */
#define IMX283_REG_DIGITAL_GAIN		CCI_REG8(0x3044)
#define IMX283_DGTL_GAIN_MIN		0
#define IMX283_DGTL_GAIN_MAX		3
#define IMX283_DGTL_GAIN_DEFAULT	0
#define IMX283_DGTL_GAIN_STEP		1

#define IMX283_REG_HTRIMMING_START	CCI_REG16_LE(0x3058)
#define IMX283_REG_HTRIMMING_END	CCI_REG16_LE(0x305a)

#define IMX283_REG_MDSEL18		CCI_REG16_LE(0x30f6)

/* Master Mode Operation Control */
#define IMX283_REG_XMSTA		CCI_REG8(0x3105)
#define   IMX283_XMSTA			BIT(0)

#define IMX283_REG_SYNCDRV		CCI_REG8(0x3107)
#define   IMX283_SYNCDRV_XHS_XVS	(0xa0 | 0x02)
#define   IMX283_SYNCDRV_HIZ		(0xa0 | 0x03)

/* PLL Standby */
#define IMX283_REG_STBPL		CCI_REG8(0x320b)
#define  IMX283_STBPL_NORMAL		0x00
#define  IMX283_STBPL_STANDBY		0x03

/* Input Frequency Setting */
#define IMX283_REG_PLRD1		CCI_REG8(0x36c1)
#define IMX283_REG_PLRD2		CCI_REG16_LE(0x36c2)
#define IMX283_REG_PLRD3		CCI_REG8(0x36f7)
#define IMX283_REG_PLRD4		CCI_REG8(0x36f8)

#define IMX283_REG_PLSTMG02		CCI_REG8(0x36aa)
#define   IMX283_PLSTMG02_VAL		0x00

#define IMX283_REG_EBD_X_OUT_SIZE	CCI_REG16_LE(0x3a54)

/* Test pattern generator */
#define IMX283_REG_TPG_CTRL		CCI_REG8(0x3156)
#define   IMX283_TPG_CTRL_CLKEN		BIT(0)
#define   IMX283_TPG_CTRL_PATEN		BIT(4)

#define IMX283_REG_TPG_PAT		CCI_REG8(0x3157)
#define   IMX283_TPG_PAT_ALL_000	0x00
#define   IMX283_TPG_PAT_ALL_FFF	0x01
#define   IMX283_TPG_PAT_ALL_555	0x02
#define   IMX283_TPG_PAT_ALL_AAA	0x03
#define   IMX283_TPG_PAT_H_COLOR_BARS	0x0a
#define   IMX283_TPG_PAT_V_COLOR_BARS	0x0b

/* Exposure control */
#define IMX283_EXPOSURE_MIN		52
#define IMX283_EXPOSURE_STEP		1
#define IMX283_EXPOSURE_DEFAULT		1000
#define IMX283_EXPOSURE_MAX		49865

#define IMAGE_PAD			0

#define IMX283_XCLR_MIN_DELAY_US	(1 * USEC_PER_MSEC)
#define IMX283_XCLR_DELAY_RANGE_US	(1 * USEC_PER_MSEC)

/* IMX283 native and active pixel array size. */
static const struct v4l2_rect imx283_native_area = {
	.top = 0,
	.left = 0,
	.width = 5592,
	.height = 3710,
};

static const struct v4l2_rect imx283_active_area = {
	.top = 40,
	.left = 108,
	.width = 5472,
	.height = 3648,
};

struct imx283_reg_list {
	unsigned int num_of_regs;
	const struct cci_reg_sequence *regs;
};

/* Mode : resolution and related config values */
struct imx283_mode {
	unsigned int mode;

	/* Bits per pixel */
	unsigned int bpp;

	/* Frame width */
	unsigned int width;

	/* Frame height */
	unsigned int height;

	/*
	 * Minimum horizontal timing in pixel-units
	 *
	 * Note that HMAX is written in 72MHz units, and the datasheet assumes a
	 * 720MHz link frequency. Convert datasheet values with the following:
	 *
	 * For 12 bpp modes (480Mbps) convert with:
	 *   hmax = [hmax in 72MHz units] * 480 / 72
	 *
	 * For 10 bpp modes (576Mbps) convert with:
	 *   hmax = [hmax in 72MHz units] * 576 / 72
	 */
	u32 min_hmax;

	/* minimum V-timing in lines */
	u32 min_vmax;

	/* default H-timing */
	u32 default_hmax;

	/* default V-timing */
	u32 default_vmax;

	/* minimum SHR */
	u32 min_shr;

	/*
	 * Per-mode vertical crop constants used to calculate values
	 * of IMX283REG_WIDCUT and IMX283_REG_VWINPOS.
	 */
	u32 veff;
	u32 vst;
	u32 vct;

	/* Horizontal and vertical binning ratio */
	u8 hbin_ratio;
	u8 vbin_ratio;

	/* Optical Blanking */
	u32 horizontal_ob;
	u32 vertical_ob;

	/* Analog crop rectangle. */
	struct v4l2_rect crop;
};

struct imx283_input_frequency {
	unsigned int mhz;
	unsigned int reg_count;
	struct cci_reg_sequence regs[4];
};

static const struct imx283_input_frequency imx283_frequencies[] = {
	{
		.mhz = 6 * HZ_PER_MHZ,
		.reg_count = 4,
		.regs = {
			{ IMX283_REG_PLRD1, 0x00 },
			{ IMX283_REG_PLRD2, 0x00f0 },
			{ IMX283_REG_PLRD3, 0x00 },
			{ IMX283_REG_PLRD4, 0xc0 },
		},
	},
	{
		.mhz = 12 * HZ_PER_MHZ,
		.reg_count = 4,
		.regs = {
			{ IMX283_REG_PLRD1, 0x01 },
			{ IMX283_REG_PLRD2, 0x00f0 },
			{ IMX283_REG_PLRD3, 0x01 },
			{ IMX283_REG_PLRD4, 0xc0 },
		},
	},
	{
		.mhz = 18 * HZ_PER_MHZ,
		.reg_count = 4,
		.regs = {
			{ IMX283_REG_PLRD1, 0x01 },
			{ IMX283_REG_PLRD2, 0x00a0 },
			{ IMX283_REG_PLRD3, 0x01 },
			{ IMX283_REG_PLRD4, 0x80 },
		},
	},
	{
		.mhz = 24 * HZ_PER_MHZ,
		.reg_count = 4,
		.regs = {
			{ IMX283_REG_PLRD1, 0x02 },
			{ IMX283_REG_PLRD2, 0x00f0 },
			{ IMX283_REG_PLRD3, 0x02 },
			{ IMX283_REG_PLRD4, 0xc0 },
		},
	},
};

enum imx283_modes {
	IMX283_MODE_0,
	IMX283_MODE_1,
	IMX283_MODE_1A,
	IMX283_MODE_1S,
	IMX283_MODE_2,
	IMX283_MODE_2A,
	IMX283_MODE_3,
	IMX283_MODE_4,
	IMX283_MODE_5,
	IMX283_MODE_6,
};

struct imx283_readout_mode {
	u8 mdsel1;
	u8 mdsel2;
	u8 mdsel3;
	u8 mdsel4;
};

static const struct imx283_readout_mode imx283_readout_modes[] = {
	/* All pixel scan modes */
	[IMX283_MODE_0] = { 0x04, 0x03, 0x10, 0x00 }, /* 12 bit */
	[IMX283_MODE_1] = { 0x04, 0x01, 0x00, 0x00 }, /* 10 bit */
	[IMX283_MODE_1A] = { 0x04, 0x01, 0x20, 0x50 }, /* 10 bit */
	[IMX283_MODE_1S] = { 0x04, 0x41, 0x20, 0x50 }, /* 10 bit */

	/* Horizontal / Vertical 2/2-line binning */
	[IMX283_MODE_2] = { 0x0d, 0x11, 0x50, 0x00 }, /* 12 bit */
	[IMX283_MODE_2A] = { 0x0d, 0x11, 0x70, 0x50 }, /* 12 bit */

	/* Horizontal / Vertical 3/3-line binning */
	[IMX283_MODE_3] = { 0x1e, 0x18, 0x10, 0x00 }, /* 12 bit */

	/* Vertical 2/9 subsampling, horizontal 3 binning cropping */
	[IMX283_MODE_4] = { 0x29, 0x18, 0x30, 0x50 }, /* 12 bit */

	/* Vertical 2/19 subsampling binning, horizontal 3 binning */
	[IMX283_MODE_5] = { 0x2d, 0x18, 0x10, 0x00 }, /* 12 bit */

	/* Vertical 2 binning horizontal 2/4, subsampling 16:9 cropping */
	[IMX283_MODE_6] = { 0x18, 0x21, 0x00, 0x09 }, /* 10 bit */

	/*
	 * New modes should make sure the offset period is complied.
	 * See imx283_exposure() for reference.
	 */
};

static const struct cci_reg_sequence mipi_data_rate_1440Mbps[] = {
	/* The default register settings provide the 1440Mbps rate */
	{ CCI_REG8(0x36c5), 0x00 }, /* Undocumented */
	{ CCI_REG8(0x3ac4), 0x00 }, /* Undocumented */

	{ IMX283_REG_STBPL, 0x00 },
	{ IMX283_REG_TCLKPOST, 0xa7 },
	{ IMX283_REG_THSPREPARE, 0x6f },
	{ IMX283_REG_THSZERO, 0x9f },
	{ IMX283_REG_THSTRAIL, 0x5f },
	{ IMX283_REG_TCLKTRAIL, 0x5f },
	{ IMX283_REG_TCLKPREPARE, 0x6f },
	{ IMX283_REG_TCLKZERO, 0x017f },
	{ IMX283_REG_TLPX, 0x4f },
	{ IMX283_REG_THSEXIT, 0x47 },
	{ IMX283_REG_TCLKPRE, 0x07 },
	{ IMX283_REG_SYSMODE, 0x02 },
};

static const struct cci_reg_sequence mipi_data_rate_720Mbps[] = {
	/* Undocumented Additions "For 720MBps" Setting */
	{ CCI_REG8(0x36c5), 0x01 }, /* Undocumented */
	{ CCI_REG8(0x3ac4), 0x01 }, /* Undocumented */

	{ IMX283_REG_STBPL, 0x00 },
	{ IMX283_REG_TCLKPOST, 0x77 },
	{ IMX283_REG_THSPREPARE, 0x37 },
	{ IMX283_REG_THSZERO, 0x67 },
	{ IMX283_REG_THSTRAIL, 0x37 },
	{ IMX283_REG_TCLKTRAIL, 0x37 },
	{ IMX283_REG_TCLKPREPARE, 0x37 },
	{ IMX283_REG_TCLKZERO, 0xdf },
	{ IMX283_REG_TLPX, 0x2f },
	{ IMX283_REG_THSEXIT, 0x47 },
	{ IMX283_REG_TCLKPRE, 0x0f },
	{ IMX283_REG_SYSMODE, 0x02 },
};

static const s64 link_frequencies[] = {
	720 * HZ_PER_MHZ, /* 1440 Mbps lane data rate */
	360 * HZ_PER_MHZ, /* 720 Mbps data lane rate */
};

static const struct imx283_reg_list link_freq_reglist[] = {
	{ /* 720 MHz */
		.num_of_regs = ARRAY_SIZE(mipi_data_rate_1440Mbps),
		.regs = mipi_data_rate_1440Mbps,
	},
	{ /* 360 MHz */
		.num_of_regs = ARRAY_SIZE(mipi_data_rate_720Mbps),
		.regs = mipi_data_rate_720Mbps,
	},
};

/* Mode configs */
static const struct imx283_mode supported_modes_12bit[] = {
	{
		/* 20MPix 21.40 fps readout mode 0 */
		.mode = IMX283_MODE_0,
		.bpp = 12,
		.width = 5472,
		.height = 3648,
		.min_hmax = 5914, /* 887 @ 480MHz/72MHz */
		.min_vmax = 3793, /* Lines */

		.veff = 3694,
		.vst = 0,
		.vct = 0,

		.hbin_ratio = 1,
		.vbin_ratio = 1,

		/* 20.00 FPS */
		.default_hmax = 6000, /* 900 @ 480MHz/72MHz */
		.default_vmax = 4000,

		.min_shr = 11,
		.horizontal_ob = 96,
		.vertical_ob = 16,
		.crop = {
			.top = 40,
			.left = 108,
			.width = 5472,
			.height = 3648,
		},
	},
	{
		/*
		 * Readout mode 2 : 2/2 binned mode (2736x1824)
		 */
		.mode = IMX283_MODE_2,
		.bpp = 12,
		.width = 2736,
		.height = 1824,
		.min_hmax = 2414, /* Pixels (362 * 480MHz/72MHz + padding) */
		.min_vmax = 3840, /* Lines */

		/* 50.00 FPS */
		.default_hmax = 2500, /* 375 @ 480MHz/72Mhz */
		.default_vmax = 3840,

		.veff = 1824,
		.vst = 0,
		.vct = 0,

		.hbin_ratio = 2,
		.vbin_ratio = 2,

		.min_shr = 12,
		.horizontal_ob = 48,
		.vertical_ob = 4,

		.crop = {
			.top = 40,
			.left = 108,
			.width = 5472,
			.height = 3648,
		},
	},
};

static const struct imx283_mode supported_modes_10bit[] = {
	{
		/* 20MPix 25.48 fps readout mode 1 */
		.mode = IMX283_MODE_1,
		.bpp = 10,
		.width = 5472,
		.height = 3648,
		.min_hmax = 5960, /* 745 @ 576MHz / 72MHz */
		.min_vmax = 3793,

		/* 25.00 FPS */
		.default_hmax = 6000, /* 750 @ 576MHz / 72MHz */
		.default_vmax = 3840,

		.min_shr = 10,
		.horizontal_ob = 96,
		.vertical_ob = 16,
		.crop = {
			.top = 40,
			.left = 108,
			.width = 5472,
			.height = 3648,
		},
	},
};

static const u32 imx283_mbus_codes[] = {
	MEDIA_BUS_FMT_SRGGB12_1X12,
	MEDIA_BUS_FMT_SRGGB10_1X10,
};

/* regulator supplies */
static const char *const imx283_supply_name[] = {
	"vadd", /* Analog (2.9V) supply */
	"vdd1", /* Supply Voltage 2 (1.8V) supply */
	"vdd2", /* Supply Voltage 3 (1.2V) supply */
};

struct imx283 {
	struct device *dev;
	struct regmap *cci;

	const struct imx283_input_frequency *freq;

	struct v4l2_subdev sd;
	struct media_pad pad;

	struct clk *xclk;

	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[ARRAY_SIZE(imx283_supply_name)];

	/* V4L2 Controls */
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vflip;

	unsigned long link_freq_bitmap;

	u16 hmax;
	u32 vmax;
};

static inline struct imx283 *to_imx283(struct v4l2_subdev *sd)
{
	return container_of_const(sd, struct imx283, sd);
}

static inline void get_mode_table(unsigned int code,
				  const struct imx283_mode **mode_list,
				  unsigned int *num_modes)
{
	switch (code) {
	case MEDIA_BUS_FMT_SRGGB12_1X12:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
	case MEDIA_BUS_FMT_SGBRG12_1X12:
	case MEDIA_BUS_FMT_SBGGR12_1X12:
		*mode_list = supported_modes_12bit;
		*num_modes = ARRAY_SIZE(supported_modes_12bit);
		break;

	case MEDIA_BUS_FMT_SRGGB10_1X10:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SBGGR10_1X10:
		*mode_list = supported_modes_10bit;
		*num_modes = ARRAY_SIZE(supported_modes_10bit);
		break;
	default:
		*mode_list = NULL;
		*num_modes = 0;
		break;
	}
}

/* Calculate the Pixel Rate based on the current mode */
static u64 imx283_pixel_rate(struct imx283 *imx283,
			     const struct imx283_mode *mode)
{
	u64 link_frequency = link_frequencies[__ffs(imx283->link_freq_bitmap)];
	unsigned int bpp = mode->bpp;
	const unsigned int ddr = 2; /* Double Data Rate */
	const unsigned int lanes = 4; /* Only 4 lane support */
	u64 numerator = link_frequency * ddr * lanes;

	do_div(numerator, bpp);

	return numerator;
}

/* Convert from a variable pixel_rate to 72 MHz clock cycles */
static u64 imx283_internal_clock(unsigned int pixel_rate, unsigned int pixels)
{
	/*
	 * Determine the following operation without overflow:
	 *    pixels = 72 Mhz / pixel_rate
	 *
	 * The internal clock at 72MHz and Pixel Rate (between 240 and 576MHz)
	 * can easily overflow this calculation, so pre-divide to simplify.
	 */
	const u32 iclk_pre = 72;
	const u32 pclk_pre = pixel_rate / HZ_PER_MHZ;
	u64 numerator = pixels * iclk_pre;

	do_div(numerator, pclk_pre);

	return numerator;
}

/* Internal clock (72MHz) to Pixel Rate clock (Variable) */
static u64 imx283_iclk_to_pix(unsigned int pixel_rate, unsigned int cycles)
{
	/*
	 * Determine the following operation without overflow:
	 *    cycles * pixel_rate / 72 MHz
	 *
	 * The internal clock at 72MHz and Pixel Rate (between 240 and 576MHz)
	 * can easily overflow this calculation, so pre-divide to simplify.
	 */
	const u32 iclk_pre = 72;
	const u32 pclk_pre = pixel_rate / HZ_PER_MHZ;
	u64 numerator = cycles * pclk_pre;

	do_div(numerator, iclk_pre);

	return numerator;
}

/* Determine the exposure based on current hmax, vmax and a given SHR */
static u32 imx283_exposure(struct imx283 *imx283,
			   const struct imx283_mode *mode, u64 shr)
{
	u32 svr = 0; /* SVR feature is not currently supported */
	u32 offset;
	u64 numerator;

	/* Number of clocks per internal offset period */
	offset = mode->mode == IMX283_MODE_0 ? 209 : 157;
	numerator = (imx283->vmax * (svr + 1) - shr) * imx283->hmax + offset;

	do_div(numerator, imx283->hmax);

	return clamp(numerator, 0, U32_MAX);
}

static void imx283_exposure_limits(struct imx283 *imx283,
				   const struct imx283_mode *mode,
				   s64 *min_exposure, s64 *max_exposure)
{
	u32 svr = 0; /* SVR feature is not currently supported */
	u64 min_shr = mode->min_shr;
	/* Global Shutter is not supported */
	u64 max_shr = (svr + 1) * imx283->vmax - 4;

	max_shr = min(max_shr, BIT(16) - 1);

	*min_exposure = imx283_exposure(imx283, mode, max_shr);
	*max_exposure = imx283_exposure(imx283, mode, min_shr);
}

/*
 * Integration Time [s] = [ {VMAX x (SVR + 1) – (SHR)} x HMAX + offset ]
 *                      / [ 72 x 10^6 ]
 */
static u32 imx283_shr(struct imx283 *imx283, const struct imx283_mode *mode,
		      u32 exposure)
{
	u32 svr = 0; /* SVR feature is not currently supported */
	u32 offset;
	u64 temp;

	/* Number of clocks per internal offset period */
	offset = mode->mode == IMX283_MODE_0 ? 209 : 157;
	temp = ((u64)exposure * imx283->hmax - offset);
	do_div(temp, imx283->hmax);

	return (imx283->vmax * (svr + 1) - temp);
}

static const char * const imx283_tpg_menu[] = {
	"Disabled",
	"All 000h",
	"All FFFh",
	"All 555h",
	"All AAAh",
	"Horizontal color bars",
	"Vertical color bars",
};

static const int imx283_tpg_val[] = {
	IMX283_TPG_PAT_ALL_000,
	IMX283_TPG_PAT_ALL_000,
	IMX283_TPG_PAT_ALL_FFF,
	IMX283_TPG_PAT_ALL_555,
	IMX283_TPG_PAT_ALL_AAA,
	IMX283_TPG_PAT_H_COLOR_BARS,
	IMX283_TPG_PAT_V_COLOR_BARS,
};

static int imx283_update_test_pattern(struct imx283 *imx283, u32 pattern_index)
{
	int ret;

	if (pattern_index >= ARRAY_SIZE(imx283_tpg_val))
		return -EINVAL;

	if (!pattern_index)
		return cci_write(imx283->cci, IMX283_REG_TPG_CTRL, 0x00, NULL);

	ret = cci_write(imx283->cci, IMX283_REG_TPG_PAT,
			imx283_tpg_val[pattern_index], NULL);
	if (ret)
		return ret;

	return cci_write(imx283->cci, IMX283_REG_TPG_CTRL,
			 IMX283_TPG_CTRL_CLKEN | IMX283_TPG_CTRL_PATEN, NULL);
}

static int imx283_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx283 *imx283 = container_of(ctrl->handler, struct imx283,
					     ctrl_handler);
	const struct imx283_mode *mode;
	struct v4l2_mbus_framefmt *fmt;
	const struct imx283_mode *mode_list;
	struct v4l2_subdev_state *state;
	unsigned int num_modes;
	u64 shr, pixel_rate;
	int ret = 0;

	state = v4l2_subdev_get_locked_active_state(&imx283->sd);
	fmt = v4l2_subdev_state_get_format(state, 0);

	get_mode_table(fmt->code, &mode_list, &num_modes);
	mode = v4l2_find_nearest_size(mode_list, num_modes, width, height,
				      fmt->width, fmt->height);

	/*
	 * The VBLANK control may change the limits of usable exposure, so check
	 * and adjust if necessary.
	 */
	if (ctrl->id == V4L2_CID_VBLANK) {
		/* Honour the VBLANK limits when setting exposure. */
		s64 current_exposure, max_exposure, min_exposure;

		imx283->vmax = mode->height + ctrl->val;

		imx283_exposure_limits(imx283, mode,
				       &min_exposure, &max_exposure);

		current_exposure = imx283->exposure->val;
		current_exposure = clamp(current_exposure, min_exposure,
					 max_exposure);

		__v4l2_ctrl_modify_range(imx283->exposure, min_exposure,
					 max_exposure, 1, current_exposure);
	}

	/*
	 * Applying V4L2 control value only happens
	 * when power is up for streaming
	 */
	if (!pm_runtime_get_if_active(imx283->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		shr = imx283_shr(imx283, mode, ctrl->val);
		dev_dbg(imx283->dev, "V4L2_CID_EXPOSURE : %d - SHR: %lld\n",
			ctrl->val, shr);
		ret = cci_write(imx283->cci, IMX283_REG_SHR, shr, NULL);
		break;

	case V4L2_CID_HBLANK:
		pixel_rate = imx283_pixel_rate(imx283, mode);
		imx283->hmax = imx283_internal_clock(pixel_rate, mode->width + ctrl->val);
		dev_dbg(imx283->dev, "V4L2_CID_HBLANK : %d  HMAX : %u\n",
			ctrl->val, imx283->hmax);
		ret = cci_write(imx283->cci, IMX283_REG_HMAX, imx283->hmax, NULL);
		break;

	case V4L2_CID_VBLANK:
		imx283->vmax = mode->height + ctrl->val;
		dev_dbg(imx283->dev, "V4L2_CID_VBLANK : %d  VMAX : %u\n",
			ctrl->val, imx283->vmax);
		ret = cci_write(imx283->cci, IMX283_REG_VMAX, imx283->vmax, NULL);
		break;

	case V4L2_CID_ANALOGUE_GAIN:
		ret = cci_write(imx283->cci, IMX283_REG_ANALOG_GAIN, ctrl->val, NULL);
		break;

	case V4L2_CID_DIGITAL_GAIN:
		ret = cci_write(imx283->cci, IMX283_REG_DIGITAL_GAIN, ctrl->val, NULL);
		break;

	case V4L2_CID_VFLIP:
		/*
		 * VFLIP is managed by BIT(0) of IMX283_REG_HTRIMMING address, hence
		 * both need to be set simultaneously.
		 */
		if (ctrl->val) {
			cci_write(imx283->cci, IMX283_REG_HTRIMMING,
				  IMX283_HTRIMMING_EN | IMX283_MDVREV, &ret);
		} else {
			cci_write(imx283->cci, IMX283_REG_HTRIMMING,
				  IMX283_HTRIMMING_EN, &ret);
		}
		break;

	case V4L2_CID_TEST_PATTERN:
		ret = imx283_update_test_pattern(imx283, ctrl->val);
		break;

	default:
		dev_err(imx283->dev, "ctrl(id:0x%x, val:0x%x) is not handled\n",
			ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(imx283->dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx283_ctrl_ops = {
	.s_ctrl = imx283_set_ctrl,
};

static int imx283_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(imx283_mbus_codes))
		return -EINVAL;

	code->code = imx283_mbus_codes[code->index];

	return 0;
}

static int imx283_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	const struct imx283_mode *mode_list;
	unsigned int num_modes;

	get_mode_table(fse->code, &mode_list, &num_modes);

	if (fse->index >= num_modes)
		return -EINVAL;

	fse->min_width = mode_list[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = mode_list[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static void imx283_update_image_pad_format(struct imx283 *imx283,
					   const struct imx283_mode *mode,
					   struct v4l2_mbus_framefmt *format)
{
	format->width = mode->width;
	format->height = mode->height;
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_RAW;
	format->ycbcr_enc = V4L2_YCBCR_ENC_601;
	format->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	format->xfer_func = V4L2_XFER_FUNC_NONE;
}

static int imx283_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state)
{
	struct imx283 *imx283 = to_imx283(sd);
	struct v4l2_mbus_framefmt *format;
	const struct imx283_mode *mode;
	struct v4l2_rect *crop;

	/* Initialize try_fmt */
	format = v4l2_subdev_state_get_format(state, IMAGE_PAD);

	mode = &supported_modes_12bit[0];
	format->code = MEDIA_BUS_FMT_SRGGB12_1X12;
	imx283_update_image_pad_format(imx283, mode, format);

	/* Initialize crop rectangle to mode default */
	crop = v4l2_subdev_state_get_crop(state, IMAGE_PAD);
	*crop = mode->crop;

	return 0;
}

static void imx283_set_framing_limits(struct imx283 *imx283,
				      const struct imx283_mode *mode)
{
	u64 pixel_rate = imx283_pixel_rate(imx283, mode);
	u64 min_hblank, max_hblank, def_hblank;

	/* Initialise hmax and vmax for exposure calculations */
	imx283->hmax = imx283_internal_clock(pixel_rate, mode->default_hmax);
	imx283->vmax = mode->default_vmax;

	/*
	 * Horizontal Blanking
	 * Convert the HMAX_MAX (72MHz) to Pixel rate values for HBLANK_MAX
	 */
	min_hblank = mode->min_hmax - mode->width;
	max_hblank = imx283_iclk_to_pix(pixel_rate, IMX283_HMAX_MAX) - mode->width;
	def_hblank = mode->default_hmax - mode->width;
	__v4l2_ctrl_modify_range(imx283->hblank, min_hblank, max_hblank, 1,
				 def_hblank);
	__v4l2_ctrl_s_ctrl(imx283->hblank, def_hblank);

	/* Vertical Blanking */
	__v4l2_ctrl_modify_range(imx283->vblank, mode->min_vmax - mode->height,
				 IMX283_VMAX_MAX - mode->height, 1,
				 mode->default_vmax - mode->height);
	__v4l2_ctrl_s_ctrl(imx283->vblank, mode->default_vmax - mode->height);
}

static int imx283_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *format;
	const struct imx283_mode *mode;
	struct imx283 *imx283 = to_imx283(sd);
	const struct imx283_mode *mode_list;
	unsigned int num_modes;

	get_mode_table(fmt->format.code, &mode_list, &num_modes);

	mode = v4l2_find_nearest_size(mode_list, num_modes, width, height,
				      fmt->format.width, fmt->format.height);

	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	fmt->format.colorspace = V4L2_COLORSPACE_RAW;
	fmt->format.ycbcr_enc = V4L2_YCBCR_ENC_601;
	fmt->format.quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->format.xfer_func = V4L2_XFER_FUNC_NONE;

	format = v4l2_subdev_state_get_format(sd_state, 0);

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		imx283_set_framing_limits(imx283, mode);

	*format = fmt->format;

	return 0;
}

static int imx283_standby_cancel(struct imx283 *imx283)
{
	unsigned int link_freq_idx;
	int ret = 0;

	cci_write(imx283->cci, IMX283_REG_STANDBY,
		  IMX283_STBLOGIC | IMX283_STBDV, &ret);

	/* Configure PLL clocks based on the xclk */
	cci_multi_reg_write(imx283->cci, imx283->freq->regs,
			    imx283->freq->reg_count, &ret);

	dev_dbg(imx283->dev, "Using clk freq %ld MHz",
		imx283->freq->mhz / HZ_PER_MHZ);

	/* Initialise communication */
	cci_write(imx283->cci, IMX283_REG_PLSTMG08, IMX283_PLSTMG08_VAL, &ret);
	cci_write(imx283->cci, IMX283_REG_PLSTMG02, IMX283_PLSTMG02_VAL, &ret);

	/* Enable PLL */
	cci_write(imx283->cci, IMX283_REG_STBPL, IMX283_STBPL_NORMAL, &ret);

	/* Configure the MIPI link speed */
	link_freq_idx = __ffs(imx283->link_freq_bitmap);
	cci_multi_reg_write(imx283->cci, link_freq_reglist[link_freq_idx].regs,
			    link_freq_reglist[link_freq_idx].num_of_regs,
			    &ret);

	/* 1st Stabilisation period of 1 ms or more */
	usleep_range(1000, 2000);

	/* Activate */
	cci_write(imx283->cci, IMX283_REG_STANDBY, IMX283_ACTIVE, &ret);

	/* 2nd Stabilisation period of 19ms or more */
	usleep_range(19000, 20000);

	cci_write(imx283->cci, IMX283_REG_CLAMP, IMX283_CLPSQRST, &ret);
	cci_write(imx283->cci, IMX283_REG_XMSTA, 0, &ret);
	cci_write(imx283->cci, IMX283_REG_SYNCDRV, IMX283_SYNCDRV_XHS_XVS, &ret);

	return ret;
}

/* Start streaming */
static int imx283_start_streaming(struct imx283 *imx283,
				  struct v4l2_subdev_state *state)
{
	const struct imx283_readout_mode *readout;
	const struct imx283_mode *mode;
	const struct v4l2_mbus_framefmt *fmt;
	const struct imx283_mode *mode_list;
	unsigned int num_modes;
	u32 v_widcut;
	s32 v_pos;
	u32 write_v_size;
	u32 y_out_size;
	int ret = 0;

	fmt = v4l2_subdev_state_get_format(state, 0);
	get_mode_table(fmt->code, &mode_list, &num_modes);
	mode = v4l2_find_nearest_size(mode_list, num_modes, width, height,
				      fmt->width, fmt->height);

	ret = imx283_standby_cancel(imx283);
	if (ret) {
		dev_err(imx283->dev, "failed to cancel standby\n");
		return ret;
	}

	/*
	 * Set the readout mode registers.
	 * MDSEL3 and MDSEL4 are updated to enable Arbitrary Vertical Cropping.
	 */
	readout = &imx283_readout_modes[mode->mode];
	cci_write(imx283->cci, IMX283_REG_MDSEL1, readout->mdsel1, &ret);
	cci_write(imx283->cci, IMX283_REG_MDSEL2, readout->mdsel2, &ret);
	cci_write(imx283->cci, IMX283_REG_MDSEL3,
		  readout->mdsel3 | IMX283_MDSEL3_VCROP_EN, &ret);
	cci_write(imx283->cci, IMX283_REG_MDSEL4,
		  readout->mdsel4 | IMX283_MDSEL4_VCROP_EN, &ret);

	/* Mode 1S specific entries from the Readout Drive Mode Tables */
	if (mode->mode == IMX283_MODE_1S) {
		cci_write(imx283->cci, IMX283_REG_MDSEL7, 0x01, &ret);
		cci_write(imx283->cci, IMX283_REG_MDSEL18, 0x1098, &ret);
	}

	if (ret) {
		dev_err(imx283->dev, "failed to set readout\n");
		return ret;
	}

	/* Initialise SVR. Unsupported for now - Always 0 */
	cci_write(imx283->cci, IMX283_REG_SVR, 0x00, &ret);

	dev_dbg(imx283->dev, "Mode: Size %d x %d\n", mode->width, mode->height);
	dev_dbg(imx283->dev, "Analogue Crop (in the mode) %d,%d %dx%d\n",
		mode->crop.left,
		mode->crop.top,
		mode->crop.width,
		mode->crop.height);

	y_out_size = mode->crop.height / mode->vbin_ratio;
	write_v_size = y_out_size + mode->vertical_ob;
	/*
	 * cropping start position = (VWINPOS – Vst) × 2
	 * cropping width = Veff – (VWIDCUT – Vct) × 2
	 */
	v_pos = imx283->vflip->val ?
		((-mode->crop.top / mode->vbin_ratio) / 2) + mode->vst :
		((mode->crop.top / mode->vbin_ratio) / 2)  + mode->vst;
	v_widcut = ((mode->veff - y_out_size) / 2) + mode->vct;

	cci_write(imx283->cci, IMX283_REG_Y_OUT_SIZE, y_out_size, &ret);
	cci_write(imx283->cci, IMX283_REG_WRITE_VSIZE, write_v_size, &ret);
	cci_write(imx283->cci, IMX283_REG_VWIDCUT, v_widcut, &ret);
	cci_write(imx283->cci, IMX283_REG_VWINPOS, v_pos, &ret);

	cci_write(imx283->cci, IMX283_REG_OB_SIZE_V, mode->vertical_ob, &ret);

	/* TODO: Validate mode->crop is fully contained within imx283_native_area */
	cci_write(imx283->cci, IMX283_REG_HTRIMMING_START, mode->crop.left, &ret);
	cci_write(imx283->cci, IMX283_REG_HTRIMMING_END,
		  mode->crop.left + mode->crop.width, &ret);

	/* Disable embedded data */
	cci_write(imx283->cci, IMX283_REG_EBD_X_OUT_SIZE, 0, &ret);

	/* Apply customized values from controls (HMAX/VMAX/SHR) */
	ret =  __v4l2_ctrl_handler_setup(imx283->sd.ctrl_handler);

	return ret;
}

static int imx283_enable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state, u32 pad,
				 u64 streams_mask)
{
	struct imx283 *imx283 = to_imx283(sd);
	int ret;

	if (pad != IMAGE_PAD)
		return -EINVAL;

	ret = pm_runtime_get_sync(imx283->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(imx283->dev);
		return ret;
	}

	ret = imx283_start_streaming(imx283, state);
	if (ret)
		goto err_rpm_put;

	return 0;

err_rpm_put:
	pm_runtime_mark_last_busy(imx283->dev);
	pm_runtime_put_autosuspend(imx283->dev);

	return ret;
}

static int imx283_disable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state, u32 pad,
				  u64 streams_mask)
{
	struct imx283 *imx283 = to_imx283(sd);
	int ret;

	if (pad != IMAGE_PAD)
		return -EINVAL;

	ret = cci_write(imx283->cci, IMX283_REG_STANDBY, IMX283_STBLOGIC, NULL);
	if (ret)
		dev_err(imx283->dev, "Failed to stop stream\n");

	pm_runtime_mark_last_busy(imx283->dev);
	pm_runtime_put_autosuspend(imx283->dev);

	return ret;
}

/* Power/clock management functions */
static int imx283_power_on(struct imx283 *imx283)
{
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(imx283_supply_name),
				    imx283->supplies);
	if (ret) {
		dev_err(imx283->dev, "failed to enable regulators\n");
		return ret;
	}

	ret = clk_prepare_enable(imx283->xclk);
	if (ret) {
		dev_err(imx283->dev, "failed to enable clock\n");
		goto reg_off;
	}

	gpiod_set_value_cansleep(imx283->reset_gpio, 0);

	usleep_range(IMX283_XCLR_MIN_DELAY_US,
		     IMX283_XCLR_MIN_DELAY_US + IMX283_XCLR_DELAY_RANGE_US);

	return 0;

reg_off:
	regulator_bulk_disable(ARRAY_SIZE(imx283_supply_name), imx283->supplies);
	return ret;
}

static int imx283_power_off(struct imx283 *imx283)
{
	gpiod_set_value_cansleep(imx283->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(imx283_supply_name), imx283->supplies);
	clk_disable_unprepare(imx283->xclk);

	return 0;
}

static int imx283_runtime_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx283 *imx283 = to_imx283(sd);

	return imx283_power_on(imx283);
}

static int imx283_runtime_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx283 *imx283 = to_imx283(sd);

	imx283_power_off(imx283);

	return 0;
}

static int imx283_get_regulators(struct imx283 *imx283)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(imx283_supply_name); i++)
		imx283->supplies[i].supply = imx283_supply_name[i];

	return devm_regulator_bulk_get(imx283->dev,
				       ARRAY_SIZE(imx283_supply_name),
				       imx283->supplies);
}

/* Verify chip ID */
static int imx283_identify_module(struct imx283 *imx283)
{
	int ret;
	u64 val;

	ret = cci_read(imx283->cci, IMX283_REG_CHIP_ID, &val, NULL);
	if (ret) {
		dev_err(imx283->dev, "failed to read chip id %x, with error %d\n",
			IMX283_CHIP_ID, ret);
		return ret;
	}

	if (val != IMX283_CHIP_ID) {
		dev_err(imx283->dev, "chip id mismatch: %x!=%llx\n",
			IMX283_CHIP_ID, val);
		return -EIO;
	}

	return 0;
}

static int imx283_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP: {
		sel->r = *v4l2_subdev_state_get_crop(sd_state, 0);
		return 0;
	}

	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r = imx283_native_area;
		return 0;

	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r = imx283_active_area;
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct v4l2_subdev_core_ops imx283_core_ops = {
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops imx283_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops imx283_pad_ops = {
	.enum_mbus_code = imx283_enum_mbus_code,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = imx283_set_pad_format,
	.get_selection = imx283_get_selection,
	.enum_frame_size = imx283_enum_frame_size,
	.enable_streams = imx283_enable_streams,
	.disable_streams = imx283_disable_streams,
};

static const struct v4l2_subdev_internal_ops imx283_internal_ops = {
	.init_state = imx283_init_state,
};

static const struct v4l2_subdev_ops imx283_subdev_ops = {
	.core = &imx283_core_ops,
	.video = &imx283_video_ops,
	.pad = &imx283_pad_ops,
};

/* Initialize control handlers */
static int imx283_init_controls(struct imx283 *imx283)
{
	struct v4l2_ctrl_handler *ctrl_hdlr;
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl *link_freq;
	const struct imx283_mode *mode = &supported_modes_12bit[0];
	u64 min_hblank, max_hblank, def_hblank;
	u64 pixel_rate;
	int ret;

	ctrl_hdlr = &imx283->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 16);
	if (ret)
		return ret;

	/*
	 * Create the controls here, but mode specific limits are setup
	 * in the imx283_set_framing_limits() call below.
	 */

	/* By default, PIXEL_RATE is read only */
	pixel_rate = imx283_pixel_rate(imx283, mode);
	v4l2_ctrl_new_std(ctrl_hdlr, &imx283_ctrl_ops,
			  V4L2_CID_PIXEL_RATE, pixel_rate,
			  pixel_rate, 1, pixel_rate);

	link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr, &imx283_ctrl_ops,
					   V4L2_CID_LINK_FREQ,
					   __fls(imx283->link_freq_bitmap),
					   __ffs(imx283->link_freq_bitmap),
					   link_frequencies);
	if (link_freq)
		link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	/* Initialise vblank/hblank/exposure based on the current mode. */
	imx283->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx283_ctrl_ops,
					   V4L2_CID_VBLANK,
					   mode->min_vmax - mode->height,
					   IMX283_VMAX_MAX, 1,
					   mode->default_vmax - mode->height);

	min_hblank = mode->min_hmax - mode->width;
	max_hblank = imx283_iclk_to_pix(pixel_rate, IMX283_HMAX_MAX) - mode->width;
	def_hblank = mode->default_hmax - mode->width;
	imx283->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx283_ctrl_ops,
					   V4L2_CID_HBLANK, min_hblank, max_hblank,
					   1, def_hblank);

	imx283->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &imx283_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX283_EXPOSURE_MIN,
					     IMX283_EXPOSURE_MAX,
					     IMX283_EXPOSURE_STEP,
					     IMX283_EXPOSURE_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx283_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  IMX283_ANA_GAIN_MIN, IMX283_ANA_GAIN_MAX,
			  IMX283_ANA_GAIN_STEP, IMX283_ANA_GAIN_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx283_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  IMX283_DGTL_GAIN_MIN, IMX283_DGTL_GAIN_MAX,
			  IMX283_DGTL_GAIN_STEP, IMX283_DGTL_GAIN_DEFAULT);

	imx283->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx283_ctrl_ops, V4L2_CID_VFLIP,
					  0, 1, 1, 0);
	if (imx283->vflip)
		imx283->vflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &imx283_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(imx283_tpg_menu) - 1,
				     0, 0, imx283_tpg_menu);

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(imx283->dev, "control init failed (%d)\n", ret);
		goto error;
	}

	ret = v4l2_fwnode_device_parse(imx283->dev, &props);
	if (ret)
		goto error;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &imx283_ctrl_ops,
					      &props);
	if (ret)
		goto error;

	imx283->sd.ctrl_handler = ctrl_hdlr;

	mutex_lock(imx283->ctrl_handler.lock);

	/* Setup exposure and frame/line length limits. */
	imx283_set_framing_limits(imx283, mode);

	mutex_unlock(imx283->ctrl_handler.lock);

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);

	return ret;
}

static int imx283_parse_endpoint(struct imx283 *imx283)
{
	struct fwnode_handle *fwnode;
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	struct fwnode_handle *ep;
	int ret;

	fwnode = dev_fwnode(imx283->dev);
	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep) {
		dev_err(imx283->dev, "Failed to get next endpoint\n");
		return -ENXIO;
	}

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != 4) {
		dev_err(imx283->dev,
			"number of CSI2 data lanes %d is not supported\n",
			bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto done_endpoint_free;
	}

	ret = v4l2_link_freq_to_bitmap(imx283->dev, bus_cfg.link_frequencies,
				       bus_cfg.nr_of_link_frequencies,
				       link_frequencies, ARRAY_SIZE(link_frequencies),
				       &imx283->link_freq_bitmap);

done_endpoint_free:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
};

static int imx283_probe(struct i2c_client *client)
{
	struct imx283 *imx283;
	unsigned int i;
	unsigned int xclk_freq;
	int ret;

	imx283 = devm_kzalloc(&client->dev, sizeof(*imx283), GFP_KERNEL);
	if (!imx283)
		return -ENOMEM;

	imx283->dev = &client->dev;

	v4l2_i2c_subdev_init(&imx283->sd, client, &imx283_subdev_ops);

	imx283->cci = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(imx283->cci)) {
		ret = PTR_ERR(imx283->cci);
		dev_err(imx283->dev, "failed to initialize CCI: %d\n", ret);
		return ret;
	}

	/* Get system clock (xclk) */
	imx283->xclk = devm_clk_get(imx283->dev, NULL);
	if (IS_ERR(imx283->xclk)) {
		return dev_err_probe(imx283->dev, PTR_ERR(imx283->xclk),
				     "failed to get xclk\n");
	}

	xclk_freq = clk_get_rate(imx283->xclk);
	for (i = 0; i < ARRAY_SIZE(imx283_frequencies); i++) {
		if (xclk_freq == imx283_frequencies[i].mhz) {
			imx283->freq = &imx283_frequencies[i];
			break;
		}
	}
	if (!imx283->freq) {
		dev_err(imx283->dev, "xclk frequency unsupported: %d Hz\n", xclk_freq);
		return -EINVAL;
	}

	ret = imx283_get_regulators(imx283);
	if (ret) {
		return dev_err_probe(imx283->dev, ret,
				"failed to get regulators\n");
	}

	ret = imx283_parse_endpoint(imx283);
	if (ret) {
		dev_err(imx283->dev, "failed to parse endpoint configuration\n");
		return ret;
	}

	/* Request optional enable pin */
	imx283->reset_gpio = devm_gpiod_get_optional(imx283->dev, "reset",
						     GPIOD_OUT_LOW);
	if (IS_ERR(imx283->reset_gpio))
		return dev_err_probe(imx283->dev, PTR_ERR(imx283->reset_gpio),
				     "failed to get reset GPIO\n");

	/*
	 * The sensor must be powered for imx283_identify_module()
	 * to be able to read the CHIP_ID register
	 */
	ret = imx283_power_on(imx283);
	if (ret)
		return ret;

	ret = imx283_identify_module(imx283);
	if (ret)
		goto error_power_off;

	/*
	 * Enable runtime PM with autosuspend. As the device has been powered
	 * manually, mark it as active, and increase the usage count without
	 * resuming the device.
	 */
	pm_runtime_set_active(imx283->dev);
	pm_runtime_get_noresume(imx283->dev);
	pm_runtime_enable(imx283->dev);
	pm_runtime_set_autosuspend_delay(imx283->dev, 1000);
	pm_runtime_use_autosuspend(imx283->dev);

	/* This needs the pm runtime to be registered. */
	ret = imx283_init_controls(imx283);
	if (ret)
		goto error_pm;

	/* Initialize subdev */
	imx283->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			    V4L2_SUBDEV_FL_HAS_EVENTS;
	imx283->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	imx283->sd.internal_ops = &imx283_internal_ops;

	/* Initialize source pads */
	imx283->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&imx283->sd.entity, 1, &imx283->pad);
	if (ret) {
		dev_err(imx283->dev, "failed to init entity pads: %d\n", ret);
		goto error_handler_free;
	}

	imx283->sd.state_lock = imx283->ctrl_handler.lock;
	ret = v4l2_subdev_init_finalize(&imx283->sd);
	if (ret < 0) {
		dev_err(imx283->dev, "subdev init error: %d\n", ret);
		goto error_media_entity;
	}

	ret = v4l2_async_register_subdev_sensor(&imx283->sd);
	if (ret < 0) {
		dev_err(imx283->dev, "failed to register sensor sub-device: %d\n", ret);
		goto error_subdev_cleanup;
	}

	/*
	 * Decrease the PM usage count. The device will get suspended after the
	 * autosuspend delay, turning the power off.
	 */
	pm_runtime_mark_last_busy(imx283->dev);
	pm_runtime_put_autosuspend(imx283->dev);

	return 0;

error_subdev_cleanup:
	v4l2_subdev_cleanup(&imx283->sd);

error_media_entity:
	media_entity_cleanup(&imx283->sd.entity);

error_handler_free:
	v4l2_ctrl_handler_free(imx283->sd.ctrl_handler);

error_pm:
	pm_runtime_disable(imx283->dev);
	pm_runtime_set_suspended(imx283->dev);
error_power_off:
	imx283_power_off(imx283);

	return ret;
}

static void imx283_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx283 *imx283 = to_imx283(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(&imx283->sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(imx283->sd.ctrl_handler);

	pm_runtime_disable(imx283->dev);
	if (!pm_runtime_status_suspended(imx283->dev))
		imx283_power_off(imx283);
	pm_runtime_set_suspended(imx283->dev);
}

static DEFINE_RUNTIME_DEV_PM_OPS(imx283_pm_ops, imx283_runtime_suspend,
				 imx283_runtime_resume, NULL);

static const struct of_device_id imx283_dt_ids[] = {
	{ .compatible = "sony,imx283" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx283_dt_ids);

static struct i2c_driver imx283_i2c_driver = {
	.driver = {
		.name = "imx283",
		.pm = pm_ptr(&imx283_pm_ops),
		.of_match_table	= imx283_dt_ids,
	},
	.probe = imx283_probe,
	.remove = imx283_remove,
};
module_i2c_driver(imx283_i2c_driver);

MODULE_AUTHOR("Will Whang <will@willwhang.com>");
MODULE_AUTHOR("Kieran Bingham <kieran.bingham@ideasonboard.com>");
MODULE_AUTHOR("Umang Jain <umang.jain@ideasonboard.com>");
MODULE_DESCRIPTION("Sony IMX283 Sensor Driver");
MODULE_LICENSE("GPL");

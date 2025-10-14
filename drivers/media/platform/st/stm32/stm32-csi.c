// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for STM32 Camera Serial Interface
 *
 * Copyright (C) STMicroelectronics SA 2024
 * Author: Alain Volmat <alain.volmat@foss.st.com>
 * for STMicroelectronics.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include <media/mipi-csi2.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define STM32_CSI_CR				0x0000
#define STM32_CSI_CR_CSIEN			BIT(0)
#define STM32_CSI_CR_VCXSTART(x)		BIT(2 + ((x) * 4))
#define STM32_CSI_CR_VCXSTOP(x)			BIT(3 + ((x) * 4))
#define STM32_CSI_PCR				0x0004
#define STM32_CSI_PCR_DL1EN			BIT(3)
#define STM32_CSI_PCR_DL0EN			BIT(2)
#define STM32_CSI_PCR_CLEN			BIT(1)
#define STM32_CSI_PCR_PWRDOWN			BIT(0)
#define STM32_CSI_VCXCFGR1(x)			((((x) + 1) * 0x0010) + 0x0)
#define STM32_CSI_VCXCFGR1_ALLDT		BIT(0)
#define STM32_CSI_VCXCFGR1_DT0EN		BIT(1)
#define STM32_CSI_VCXCFGR1_DT1EN		BIT(2)
#define STM32_CSI_VCXCFGR1_CDTFT_SHIFT		8
#define STM32_CSI_VCXCFGR1_DT0_SHIFT		16
#define STM32_CSI_VCXCFGR1_DT0FT_SHIFT		24
#define STM32_CSI_VCXCFGR2(x)			((((x) + 1) * 0x0010) + 0x4)
#define STM32_CSI_VCXCFGR2_DT1_SHIFT		0
#define STM32_CSI_VCXCFGR2_DT1FT_SHIFT		8
#define STM32_CSI_INPUT_BPP8			2
#define STM32_CSI_INPUT_BPP10			3
#define STM32_CSI_INPUT_BPP12			4
#define STM32_CSI_INPUT_BPP14			5
#define STM32_CSI_LMCFGR			0x0070
#define STM32_CSI_LMCFGR_LANENB_SHIFT		8
#define STM32_CSI_LMCFGR_DLMAP_SHIFT		16
#define STM32_CSI_IER0				0x0080
#define STM32_CSI_IER1				0x0084
#define STM32_CSI_SR0				0x0090
#define STM32_CSI_SR0_SYNCERRF			BIT(30)
#define STM32_CSI_SR0_SPKTERRF			BIT(28)
#define STM32_CSI_SR0_IDERRF			BIT(27)
#define STM32_CSI_SR0_CECCERRF			BIT(26)
#define STM32_CSI_SR0_ECCERRF			BIT(25)
#define STM32_CSI_SR0_CRCERRF			BIT(24)
#define STM32_CSI_SR0_CCFIFOFF			BIT(21)
#define STM32_CSI_SR0_VCXSTATEF(x)		BIT(17 + (x))
#define STM32_CSI_SR1				0x0094
#define STM32_CSI_SR1_ECTRLDL1F			BIT(12)
#define STM32_CSI_SR1_ESYNCESCDL1F		BIT(11)
#define STM32_CSI_SR1_EESCDL1F			BIT(10)
#define STM32_CSI_SR1_ESOTSYNCDL1F		BIT(9)
#define STM32_CSI_SR1_ESOTDL1F			BIT(8)
#define STM32_CSI_SR1_ECTRLDL0F			BIT(4)
#define STM32_CSI_SR1_ESYNCESCDL0F		BIT(3)
#define STM32_CSI_SR1_EESCDL0F			BIT(2)
#define STM32_CSI_SR1_ESOTSYNCDL0F		BIT(1)
#define STM32_CSI_SR1_ESOTDL0F			BIT(0)
#define STM32_CSI_FCR0				0x0100
#define STM32_CSI_FCR1				0x0104
#define STM32_CSI_SPDFR				0x0110
#define STM32_CSI_DT_MASK			0x3f
#define STM32_CSI_VC_MASK			0x03
#define STM32_CSI_ERR1				0x0114
#define STM32_CSI_ERR1_IDVCERR_SHIFT		22
#define STM32_CSI_ERR1_IDDTERR_SHIFT		16
#define STM32_CSI_ERR1_CECCVCERR_SHIFT		14
#define STM32_CSI_ERR1_CECCDTERR_SHIFT		8
#define STM32_CSI_ERR1_CRCVCERR_SHIFT		6
#define STM32_CSI_ERR1_CRCDTERR_SHIFT		0
#define STM32_CSI_ERR2				0x0118
#define STM32_CSI_ERR2_SYNCVCERR_SHIFT		18
#define STM32_CSI_ERR2_SPKTVCERR_SHIFT		6
#define STM32_CSI_ERR2_SPKTDTERR_SHIFT		0
#define STM32_CSI_PRCR				0x1000
#define STM32_CSI_PRCR_PEN			BIT(1)
#define STM32_CSI_PMCR				0x1004
#define STM32_CSI_PFCR				0x1008
#define STM32_CSI_PFCR_CCFR_MASK		GENMASK(5, 0)
#define STM32_CSI_PFCR_CCFR_SHIFT		0
#define STM32_CSI_PFCR_HSFR_MASK		GENMASK(14, 8)
#define STM32_CSI_PFCR_HSFR_SHIFT		8
#define STM32_CSI_PFCR_DLD			BIT(16)
#define STM32_CSI_PTCR0				0x1010
#define STM32_CSI_PTCR0_TCKEN			BIT(0)
#define STM32_CSI_PTCR1				0x1014
#define STM32_CSI_PTCR1_TWM			BIT(16)
#define STM32_CSI_PTCR1_TDI_MASK		GENMASK(7, 0)
#define STM32_CSI_PTCR1_TDI_SHIFT		0
#define STM32_CSI_PTSR				0x1018

#define STM32_CSI_LANES_MAX	2

#define STM32_CSI_SR0_ERRORS	(STM32_CSI_SR0_SYNCERRF | STM32_CSI_SR0_SPKTERRF |\
				 STM32_CSI_SR0_IDERRF | STM32_CSI_SR0_CECCERRF |\
				 STM32_CSI_SR0_ECCERRF | STM32_CSI_SR0_CRCERRF |\
				 STM32_CSI_SR0_CCFIFOFF)
#define STM32_CSI_SR1_DL0_ERRORS	(STM32_CSI_SR1_ECTRLDL0F | STM32_CSI_SR1_ESYNCESCDL0F |\
				 STM32_CSI_SR1_EESCDL0F | STM32_CSI_SR1_ESOTSYNCDL0F |\
				 STM32_CSI_SR1_ESOTDL0F)
#define STM32_CSI_SR1_DL1_ERRORS	(STM32_CSI_SR1_ECTRLDL1F | STM32_CSI_SR1_ESYNCESCDL1F |\
				 STM32_CSI_SR1_EESCDL1F | STM32_CSI_SR1_ESOTSYNCDL1F |\
				 STM32_CSI_SR1_ESOTDL1F)
#define STM32_CSI_SR1_ERRORS	(STM32_CSI_SR1_DL0_ERRORS | STM32_CSI_SR1_DL1_ERRORS)

enum stm32_csi_pads {
	STM32_CSI_PAD_SINK,
	STM32_CSI_PAD_SOURCE,
	STM32_CSI_PAD_MAX,
};

struct stm32_csi_event {
	u32 mask;
	const char * const name;
};

static const struct stm32_csi_event stm32_csi_events_sr0[] = {
	{STM32_CSI_SR0_SYNCERRF,	"Synchronization error"},
	{STM32_CSI_SR0_SPKTERRF,	"Short packet error"},
	{STM32_CSI_SR0_IDERRF,		"Data type ID error"},
	{STM32_CSI_SR0_CECCERRF,	"Corrected ECC error"},
	{STM32_CSI_SR0_ECCERRF,		"ECC error"},
	{STM32_CSI_SR0_CRCERRF,		"CRC error"},
	{STM32_CSI_SR0_CCFIFOFF,	"Clk changer FIFO full error"},
};

#define STM32_CSI_NUM_SR0_EVENTS ARRAY_SIZE(stm32_csi_events_sr0)

static const struct stm32_csi_event stm32_csi_events_sr1[] = {
	{STM32_CSI_SR1_ECTRLDL1F,	"L1: D-PHY control error"},
	{STM32_CSI_SR1_ESYNCESCDL1F,
		"L1: D-PHY low power data transmission synchro error"},
	{STM32_CSI_SR1_EESCDL1F,	"L1: D-PHY escape entry error"},
	{STM32_CSI_SR1_ESOTSYNCDL1F,
		"L1: Start of transmission synchro error"},
	{STM32_CSI_SR1_ESOTDL1F,	"L1: Start of transmission error"},
	{STM32_CSI_SR1_ECTRLDL0F,	"L0: D-PHY control error"},
	{STM32_CSI_SR1_ESYNCESCDL0F,
		"L0: D-PHY low power data transmission synchro error"},
	{STM32_CSI_SR1_EESCDL0F,	"L0: D-PHY escape entry error"},
	{STM32_CSI_SR1_ESOTSYNCDL0F,
		"L0: Start of transmission synchro error"},
	{STM32_CSI_SR1_ESOTDL0F,	"L0: Start of transmission error"},
};

#define STM32_CSI_NUM_SR1_EVENTS ARRAY_SIZE(stm32_csi_events_sr1)

enum stm32_csi_clk {
	STM32_CSI_CLK_PCLK,
	STM32_CSI_CLK_TXESC,
	STM32_CSI_CLK_CSI2PHY,
	STM32_CSI_CLK_NB,
};

static const char * const stm32_csi_clks_id[] = {
	"pclk",
	"txesc",
	"csi2phy",
};

struct stm32_csi_dev {
	struct device			*dev;

	void __iomem			*base;

	struct clk_bulk_data		clks[STM32_CSI_CLK_NB];
	struct regulator_bulk_data	supplies[2];

	u8				lanes[STM32_CSI_LANES_MAX];
	u8				num_lanes;

	/*
	 * spinlock slock is used to protect to srX_counters tables being
	 * accessed from log_status and interrupt context
	 */
	spinlock_t			slock;

	u32				sr0_counters[STM32_CSI_NUM_SR0_EVENTS];
	u32				sr1_counters[STM32_CSI_NUM_SR1_EVENTS];

	struct v4l2_subdev		sd;
	struct v4l2_async_notifier	notifier;
	struct media_pad		pads[STM32_CSI_PAD_MAX];

	/* Remote source */
	struct v4l2_subdev		*s_subdev;
	u32				s_subdev_pad_nb;
};

struct stm32_csi_fmts {
	u32 code;
	u32 datatype;
	u32 input_fmt;
	u8 bpp;
};

#define FMT_MBUS_DT_DTFMT_BPP(mbus, dt, input, byteperpixel)		\
	{								\
		.code = MEDIA_BUS_FMT_##mbus,				\
		.datatype = MIPI_CSI2_DT_##dt,				\
		.input_fmt = STM32_CSI_INPUT_##input,	\
		.bpp = byteperpixel,					\
	}
static const struct stm32_csi_fmts stm32_csi_formats[] = {
	/* YUV 422 8 bit */
	FMT_MBUS_DT_DTFMT_BPP(UYVY8_1X16, YUV422_8B, BPP8, 8),
	FMT_MBUS_DT_DTFMT_BPP(YUYV8_1X16, YUV422_8B, BPP8, 8),
	FMT_MBUS_DT_DTFMT_BPP(YVYU8_1X16, YUV422_8B, BPP8, 8),
	FMT_MBUS_DT_DTFMT_BPP(VYUY8_1X16, YUV422_8B, BPP8, 8),

	/* Raw Bayer */
	/* 8 bit */
	FMT_MBUS_DT_DTFMT_BPP(SBGGR8_1X8, RAW8, BPP8, 8),
	FMT_MBUS_DT_DTFMT_BPP(SGBRG8_1X8, RAW8, BPP8, 8),
	FMT_MBUS_DT_DTFMT_BPP(SGRBG8_1X8, RAW8, BPP8, 8),
	FMT_MBUS_DT_DTFMT_BPP(SRGGB8_1X8, RAW8, BPP8, 8),
	/* 10 bit */
	FMT_MBUS_DT_DTFMT_BPP(SRGGB10_1X10, RAW10, BPP10, 10),
	FMT_MBUS_DT_DTFMT_BPP(SGBRG10_1X10, RAW10, BPP10, 10),
	FMT_MBUS_DT_DTFMT_BPP(SGRBG10_1X10, RAW10, BPP10, 10),
	FMT_MBUS_DT_DTFMT_BPP(SRGGB10_1X10, RAW10, BPP10, 10),
	/* 12 bit */
	FMT_MBUS_DT_DTFMT_BPP(SRGGB12_1X12, RAW12, BPP12, 12),
	FMT_MBUS_DT_DTFMT_BPP(SGBRG12_1X12, RAW12, BPP12, 12),
	FMT_MBUS_DT_DTFMT_BPP(SGRBG12_1X12, RAW12, BPP12, 12),
	FMT_MBUS_DT_DTFMT_BPP(SRGGB12_1X12, RAW12, BPP12, 12),
	/* 14 bit */
	FMT_MBUS_DT_DTFMT_BPP(SRGGB14_1X14, RAW14, BPP14, 14),
	FMT_MBUS_DT_DTFMT_BPP(SGBRG14_1X14, RAW14, BPP14, 14),
	FMT_MBUS_DT_DTFMT_BPP(SGRBG14_1X14, RAW14, BPP14, 14),
	FMT_MBUS_DT_DTFMT_BPP(SRGGB14_1X14, RAW14, BPP14, 14),

	/* RGB 565 */
	FMT_MBUS_DT_DTFMT_BPP(RGB565_1X16, RGB565, BPP8, 8),

	/* JPEG (datatype isn't used) */
	FMT_MBUS_DT_DTFMT_BPP(JPEG_1X8, NULL, BPP8, 8),
};

struct stm32_csi_mbps_phy_reg {
	unsigned int mbps;
	unsigned int hsfreqrange;
	unsigned int osc_freq_target;
};

/*
 * Table describing configuration of the PHY depending on the
 * intended Bit Rate. From table 5-8 Frequency Ranges and Defaults
 * of the Synopsis DWC MIPI PHY databook
 */
static const struct stm32_csi_mbps_phy_reg snps_stm32mp25[] = {
	{ .mbps =   80,	.hsfreqrange = 0x00,	.osc_freq_target = 460 },
	{ .mbps =   90, .hsfreqrange = 0x10,	.osc_freq_target = 460 },
	{ .mbps =  100, .hsfreqrange = 0x20,	.osc_freq_target = 460 },
	{ .mbps =  110, .hsfreqrange = 0x30,	.osc_freq_target = 460 },
	{ .mbps =  120, .hsfreqrange = 0x01,	.osc_freq_target = 460 },
	{ .mbps =  130, .hsfreqrange = 0x11,	.osc_freq_target = 460 },
	{ .mbps =  140, .hsfreqrange = 0x21,	.osc_freq_target = 460 },
	{ .mbps =  150, .hsfreqrange = 0x31,	.osc_freq_target = 460 },
	{ .mbps =  160, .hsfreqrange = 0x02,	.osc_freq_target = 460 },
	{ .mbps =  170, .hsfreqrange = 0x12,	.osc_freq_target = 460 },
	{ .mbps =  180, .hsfreqrange = 0x22,	.osc_freq_target = 460 },
	{ .mbps =  190, .hsfreqrange = 0x32,	.osc_freq_target = 460 },
	{ .mbps =  205, .hsfreqrange = 0x03,	.osc_freq_target = 460 },
	{ .mbps =  220, .hsfreqrange = 0x13,	.osc_freq_target = 460 },
	{ .mbps =  235, .hsfreqrange = 0x23,	.osc_freq_target = 460 },
	{ .mbps =  250, .hsfreqrange = 0x33,	.osc_freq_target = 460 },
	{ .mbps =  275, .hsfreqrange = 0x04,	.osc_freq_target = 460 },
	{ .mbps =  300, .hsfreqrange = 0x14,	.osc_freq_target = 460 },
	{ .mbps =  325, .hsfreqrange = 0x25,	.osc_freq_target = 460 },
	{ .mbps =  350, .hsfreqrange = 0x35,	.osc_freq_target = 460 },
	{ .mbps =  400, .hsfreqrange = 0x05,	.osc_freq_target = 460 },
	{ .mbps =  450, .hsfreqrange = 0x16,	.osc_freq_target = 460 },
	{ .mbps =  500, .hsfreqrange = 0x26,	.osc_freq_target = 460 },
	{ .mbps =  550, .hsfreqrange = 0x37,	.osc_freq_target = 460 },
	{ .mbps =  600, .hsfreqrange = 0x07,	.osc_freq_target = 460 },
	{ .mbps =  650, .hsfreqrange = 0x18,	.osc_freq_target = 460 },
	{ .mbps =  700, .hsfreqrange = 0x28,	.osc_freq_target = 460 },
	{ .mbps =  750, .hsfreqrange = 0x39,	.osc_freq_target = 460 },
	{ .mbps =  800, .hsfreqrange = 0x09,	.osc_freq_target = 460 },
	{ .mbps =  850, .hsfreqrange = 0x19,	.osc_freq_target = 460 },
	{ .mbps =  900, .hsfreqrange = 0x29,	.osc_freq_target = 460 },
	{ .mbps =  950, .hsfreqrange = 0x3a,	.osc_freq_target = 460 },
	{ .mbps = 1000, .hsfreqrange = 0x0a,	.osc_freq_target = 460 },
	{ .mbps = 1050, .hsfreqrange = 0x1a,	.osc_freq_target = 460 },
	{ .mbps = 1100, .hsfreqrange = 0x2a,	.osc_freq_target = 460 },
	{ .mbps = 1150, .hsfreqrange = 0x3b,	.osc_freq_target = 460 },
	{ .mbps = 1200, .hsfreqrange = 0x0b,	.osc_freq_target = 460 },
	{ .mbps = 1250, .hsfreqrange = 0x1b,	.osc_freq_target = 460 },
	{ .mbps = 1300, .hsfreqrange = 0x2b,	.osc_freq_target = 460 },
	{ .mbps = 1350, .hsfreqrange = 0x3c,	.osc_freq_target = 460 },
	{ .mbps = 1400, .hsfreqrange = 0x0c,	.osc_freq_target = 460 },
	{ .mbps = 1450, .hsfreqrange = 0x1c,	.osc_freq_target = 460 },
	{ .mbps = 1500, .hsfreqrange = 0x2c,	.osc_freq_target = 460 },
	{ .mbps = 1550, .hsfreqrange = 0x3d,	.osc_freq_target = 285 },
	{ .mbps = 1600, .hsfreqrange = 0x0d,	.osc_freq_target = 295 },
	{ .mbps = 1650, .hsfreqrange = 0x1d,	.osc_freq_target = 304 },
	{ .mbps = 1700, .hsfreqrange = 0x2e,	.osc_freq_target = 313 },
	{ .mbps = 1750, .hsfreqrange = 0x3e,	.osc_freq_target = 322 },
	{ .mbps = 1800, .hsfreqrange = 0x0e,	.osc_freq_target = 331 },
	{ .mbps = 1850, .hsfreqrange = 0x1e,	.osc_freq_target = 341 },
	{ .mbps = 1900, .hsfreqrange = 0x2f,	.osc_freq_target = 350 },
	{ .mbps = 1950, .hsfreqrange = 0x3f,	.osc_freq_target = 359 },
	{ .mbps = 2000, .hsfreqrange = 0x0f,	.osc_freq_target = 368 },
	{ .mbps = 2050, .hsfreqrange = 0x40,	.osc_freq_target = 377 },
	{ .mbps = 2100, .hsfreqrange = 0x41,	.osc_freq_target = 387 },
	{ .mbps = 2150, .hsfreqrange = 0x42,	.osc_freq_target = 396 },
	{ .mbps = 2200, .hsfreqrange = 0x43,	.osc_freq_target = 405 },
	{ .mbps = 2250, .hsfreqrange = 0x44,	.osc_freq_target = 414 },
	{ .mbps = 2300, .hsfreqrange = 0x45,	.osc_freq_target = 423 },
	{ .mbps = 2350, .hsfreqrange = 0x46,	.osc_freq_target = 432 },
	{ .mbps = 2400, .hsfreqrange = 0x47,	.osc_freq_target = 442 },
	{ .mbps = 2450, .hsfreqrange = 0x48,	.osc_freq_target = 451 },
	{ .mbps = 2500, .hsfreqrange = 0x49,	.osc_freq_target = 460 },
};

static const struct v4l2_mbus_framefmt fmt_default = {
	.width = 640,
	.height = 480,
	.code = MEDIA_BUS_FMT_RGB565_1X16,
	.field = V4L2_FIELD_NONE,
	.colorspace = V4L2_COLORSPACE_REC709,
	.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT,
	.quantization = V4L2_QUANTIZATION_DEFAULT,
	.xfer_func = V4L2_XFER_FUNC_DEFAULT,
};

static const struct stm32_csi_fmts *stm32_csi_code_to_fmt(unsigned int code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(stm32_csi_formats); i++)
		if (stm32_csi_formats[i].code == code)
			return &stm32_csi_formats[i];

	return NULL;
}

static inline struct stm32_csi_dev *to_csidev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct stm32_csi_dev, sd);
}

static int stm32_csi_setup_lane_merger(struct stm32_csi_dev *csidev)
{
	u32 lmcfgr = 0;
	unsigned int i;

	for (i = 0; i < csidev->num_lanes; i++) {
		if (!csidev->lanes[i] || csidev->lanes[i] > STM32_CSI_LANES_MAX) {
			dev_err(csidev->dev, "Invalid lane id (%d)\n", csidev->lanes[i]);
			return -EINVAL;
		}
		lmcfgr |= (csidev->lanes[i] << ((i * 4) + STM32_CSI_LMCFGR_DLMAP_SHIFT));
	}

	lmcfgr |= (csidev->num_lanes << STM32_CSI_LMCFGR_LANENB_SHIFT);

	writel_relaxed(lmcfgr, csidev->base + STM32_CSI_LMCFGR);

	return 0;
}

static void stm32_csi_phy_reg_write(struct stm32_csi_dev *csidev,
				    u32 addr, u32 val)
{
	/* Based on sequence described at section 5.2.3.2 of DesignWave document */
	/* For writing the 4-bit testcode MSBs */
	/* Set testen to high */
	writel_relaxed(STM32_CSI_PTCR1_TWM, csidev->base + STM32_CSI_PTCR1);

	/* Set testclk to high */
	writel_relaxed(STM32_CSI_PTCR0_TCKEN, csidev->base + STM32_CSI_PTCR0);

	/* Place 0x00 in testdin */
	writel_relaxed(STM32_CSI_PTCR1_TWM, csidev->base + STM32_CSI_PTCR1);

	/*
	 * Set testclk to low (with the falling edge on testclk, the testdin
	 * signal content is latched internally)
	 */
	writel_relaxed(0, csidev->base + STM32_CSI_PTCR0);

	/* Set testen to low */
	writel_relaxed(0, csidev->base + STM32_CSI_PTCR1);

	/* Place the 8-bit word corresponding to the testcode MSBs in testdin */
	writel_relaxed(((addr >> 8) & STM32_CSI_PTCR1_TDI_MASK) << STM32_CSI_PTCR1_TDI_SHIFT,
		       csidev->base + STM32_CSI_PTCR1);

	/* Set testclk to high */
	writel_relaxed(STM32_CSI_PTCR0_TCKEN, csidev->base + STM32_CSI_PTCR0);

	/* For writing the 8-bit testcode LSBs */
	/* Set testclk to low */
	writel_relaxed(0, csidev->base + STM32_CSI_PTCR0);

	/* Set testen to high */
	writel_relaxed(STM32_CSI_PTCR1_TWM, csidev->base + STM32_CSI_PTCR1);

	/* Set testclk to high */
	writel_relaxed(STM32_CSI_PTCR0_TCKEN, csidev->base + STM32_CSI_PTCR0);

	/* Place the 8-bit word test data in testdin */
	writel_relaxed((addr & STM32_CSI_PTCR1_TDI_MASK) <<
		       STM32_CSI_PTCR1_TDI_SHIFT | STM32_CSI_PTCR1_TWM,
		       csidev->base + STM32_CSI_PTCR1);

	/*
	 * Set testclk to low (with the falling edge on testclk, the testdin
	 * signal content is latched internally)
	 */
	writel_relaxed(0, csidev->base + STM32_CSI_PTCR0);

	/* Set testen to low */
	writel_relaxed(0, csidev->base + STM32_CSI_PTCR1);

	/* For writing the data */
	/* Place the 8-bit word corresponding to the page offset in testdin */
	writel_relaxed((val & STM32_CSI_PTCR1_TDI_MASK) << STM32_CSI_PTCR1_TDI_SHIFT,
		       csidev->base + STM32_CSI_PTCR1);

	/* Set testclk to high (test data is programmed internally */
	writel_relaxed(STM32_CSI_PTCR0_TCKEN, csidev->base + STM32_CSI_PTCR0);

	/* Finish by setting testclk to low */
	writel_relaxed(0, csidev->base + STM32_CSI_PTCR0);
}

static int stm32_csi_start(struct stm32_csi_dev *csidev,
			   struct v4l2_subdev_state *state)
{
	struct media_pad *src_pad;
	const struct stm32_csi_mbps_phy_reg *phy_regs = NULL;
	struct v4l2_mbus_framefmt *sink_fmt;
	const struct stm32_csi_fmts *fmt;
	unsigned long phy_clk_frate;
	u32 lanes_ie, lanes_en;
	unsigned int mbps;
	unsigned int i;
	s64 link_freq;
	int ret;
	u32 ccfr;

	dev_dbg(csidev->dev, "Starting the CSI2\n");

	/* Get the bpp value on pad0 (input of CSI) */
	sink_fmt = v4l2_subdev_state_get_format(state, STM32_CSI_PAD_SINK);
	fmt = stm32_csi_code_to_fmt(sink_fmt->code);

	/* Get the remote sensor link frequency */
	if (!csidev->s_subdev)
		return -EIO;

	src_pad = &csidev->s_subdev->entity.pads[csidev->s_subdev_pad_nb];
	link_freq = v4l2_get_link_freq(src_pad,
				       fmt->bpp, 2 * csidev->num_lanes);
	if (link_freq < 0)
		return link_freq;

	/* MBPS is expressed in Mbps, hence link_freq / 100000 * 2 */
	mbps = div_s64(link_freq, 500000);
	dev_dbg(csidev->dev, "Computed Mbps: %u\n", mbps);

	for (i = 0; i < ARRAY_SIZE(snps_stm32mp25); i++) {
		if (snps_stm32mp25[i].mbps >= mbps) {
			phy_regs = &snps_stm32mp25[i];
			break;
		}
	}

	if (!phy_regs) {
		dev_err(csidev->dev, "Unsupported PHY speed (%u Mbps)", mbps);
		return -ERANGE;
	}

	dev_dbg(csidev->dev, "PHY settings: (%u Mbps, %u HS FRange, %u OSC Freq)\n",
		phy_regs->mbps, phy_regs->hsfreqrange,
		phy_regs->osc_freq_target);

	/* Prepare lanes related configuration bits */
	lanes_ie = STM32_CSI_SR1_DL0_ERRORS;
	lanes_en = STM32_CSI_PCR_DL0EN;
	if (csidev->num_lanes == 2) {
		lanes_ie |= STM32_CSI_SR1_DL1_ERRORS;
		lanes_en |= STM32_CSI_PCR_DL1EN;
	}

	ret = pm_runtime_get_sync(csidev->dev);
	if (ret < 0)
		goto error_put;

	/* Retrieve CSI2PHY clock rate to compute CCFR value */
	phy_clk_frate = clk_get_rate(csidev->clks[STM32_CSI_CLK_CSI2PHY].clk);
	if (!phy_clk_frate) {
		dev_err(csidev->dev, "CSI2PHY clock rate invalid (0)\n");
		ret = -EINVAL;
		goto error_put;
	}

	ret = stm32_csi_setup_lane_merger(csidev);
	if (ret)
		goto error_put;

	/* Enable the CSI */
	writel_relaxed(STM32_CSI_CR_CSIEN, csidev->base + STM32_CSI_CR);

	/* Enable some global CSI related interrupts - bits are same as SR0 */
	writel_relaxed(STM32_CSI_SR0_ERRORS, csidev->base + STM32_CSI_IER0);

	/* Enable lanes related error interrupts */
	writel_relaxed(lanes_ie, csidev->base + STM32_CSI_IER1);

	/* Initialization of the D-PHY */
	/* Stop the D-PHY */
	writel_relaxed(0, csidev->base + STM32_CSI_PRCR);

	/* Keep the D-PHY in power down state */
	writel_relaxed(0, csidev->base + STM32_CSI_PCR);

	/* Enable testclr clock during 15ns */
	writel_relaxed(STM32_CSI_PTCR0_TCKEN, csidev->base + STM32_CSI_PTCR0);
	udelay(1);
	writel_relaxed(0, csidev->base + STM32_CSI_PTCR0);

	/* Set hsfreqrange */
	phy_clk_frate /= 1000000;
	ccfr = (phy_clk_frate - 17) * 4;
	writel_relaxed((ccfr << STM32_CSI_PFCR_CCFR_SHIFT) |
		       (phy_regs->hsfreqrange << STM32_CSI_PFCR_HSFR_SHIFT),
		       csidev->base + STM32_CSI_PFCR);

	/* set reg @08 deskew_polarity_rw 1'b1 */
	stm32_csi_phy_reg_write(csidev, 0x08, 0x38);

	/* set reg @0xE4 counter_for_des_en_config_if_rx 0x10 + DLL prog EN */
	/* This is because 13<= cfgclkfreqrange[5:0]<=38 */
	stm32_csi_phy_reg_write(csidev, 0xe4, 0x11);

	/* set reg @0xe2 & reg @0xe3 value DLL target oscilation freq */
	/* Based on the table page 77, osc_freq_target */
	stm32_csi_phy_reg_write(csidev, 0xe2, phy_regs->osc_freq_target & 0xFF);
	stm32_csi_phy_reg_write(csidev, 0xe3, (phy_regs->osc_freq_target >> 8) & 0x0F);

	writel_relaxed(STM32_CSI_PFCR_DLD | readl_relaxed(csidev->base + STM32_CSI_PFCR),
		       csidev->base + STM32_CSI_PFCR);

	/* Enable Lanes */
	writel_relaxed(lanes_en | STM32_CSI_PCR_CLEN, csidev->base + STM32_CSI_PCR);
	writel_relaxed(lanes_en | STM32_CSI_PCR_CLEN | STM32_CSI_PCR_PWRDOWN,
		       csidev->base + STM32_CSI_PCR);

	writel_relaxed(STM32_CSI_PRCR_PEN, csidev->base + STM32_CSI_PRCR);

	/* Remove the force */
	writel_relaxed(0, csidev->base + STM32_CSI_PMCR);

	return ret;

error_put:
	pm_runtime_put(csidev->dev);
	return ret;
}

static void stm32_csi_stop(struct stm32_csi_dev *csidev)
{
	dev_dbg(csidev->dev, "Stopping the CSI2\n");

	/* Disable the D-PHY */
	writel_relaxed(0, csidev->base + STM32_CSI_PCR);

	/* Disable ITs */
	writel_relaxed(0, csidev->base + STM32_CSI_IER0);
	writel_relaxed(0, csidev->base + STM32_CSI_IER1);

	/* Disable the CSI */
	writel_relaxed(0, csidev->base + STM32_CSI_CR);

	pm_runtime_put(csidev->dev);
}

static int stm32_csi_start_vc(struct stm32_csi_dev *csidev,
			      struct v4l2_subdev_state *state, u32 vc)
{
	struct v4l2_mbus_framefmt *mbus_fmt;
	const struct stm32_csi_fmts *fmt;
	u32 status;
	u32 cfgr1;
	int ret;

	mbus_fmt = v4l2_subdev_state_get_format(state, STM32_CSI_PAD_SOURCE);
	fmt = stm32_csi_code_to_fmt(mbus_fmt->code);

	/* If the mbus code is JPEG, don't enable filtering */
	if (mbus_fmt->code == MEDIA_BUS_FMT_JPEG_1X8) {
		cfgr1 = STM32_CSI_VCXCFGR1_ALLDT;
		cfgr1 |= fmt->input_fmt << STM32_CSI_VCXCFGR1_CDTFT_SHIFT;
		dev_dbg(csidev->dev, "VC%d: enable AllDT mode\n", vc);
	} else {
		cfgr1 = fmt->datatype << STM32_CSI_VCXCFGR1_DT0_SHIFT;
		cfgr1 |= fmt->input_fmt << STM32_CSI_VCXCFGR1_DT0FT_SHIFT;
		cfgr1 |= STM32_CSI_VCXCFGR1_DT0EN;
		dev_dbg(csidev->dev, "VC%d: enable DT0(0x%x)/DT0FT(0x%x)\n",
			vc, fmt->datatype, fmt->input_fmt);
	}
	writel_relaxed(cfgr1, csidev->base + STM32_CSI_VCXCFGR1(vc));

	/* Enable processing of the virtual-channel and wait for its status */
	writel_relaxed(STM32_CSI_CR_VCXSTART(vc) | STM32_CSI_CR_CSIEN,
		       csidev->base + STM32_CSI_CR);

	ret = readl_relaxed_poll_timeout(csidev->base + STM32_CSI_SR0,
					 status,
					 status & STM32_CSI_SR0_VCXSTATEF(vc),
					 1000, 1000000);
	if (ret) {
		dev_err(csidev->dev, "failed to start VC(%d)\n", vc);
		return ret;
	}

	return 0;
}

static int stm32_csi_stop_vc(struct stm32_csi_dev *csidev, u32 vc)
{
	u32 status;
	int ret;

	/* Stop the Virtual Channel */
	writel_relaxed(STM32_CSI_CR_VCXSTOP(vc) | STM32_CSI_CR_CSIEN,
		       csidev->base + STM32_CSI_CR);

	ret = readl_relaxed_poll_timeout(csidev->base + STM32_CSI_SR0,
					 status,
					 !(status & STM32_CSI_SR0_VCXSTATEF(vc)),
					 1000, 1000000);
	if (ret) {
		dev_err(csidev->dev, "failed to stop VC(%d)\n", vc);
		return ret;
	}

	/* Disable all DTs */
	writel_relaxed(0, csidev->base + STM32_CSI_VCXCFGR1(vc));
	writel_relaxed(0, csidev->base + STM32_CSI_VCXCFGR2(vc));

	return 0;
}

static int stm32_csi_disable_streams(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *state, u32 pad,
				     u64 streams_mask)
{
	struct stm32_csi_dev *csidev = to_csidev(sd);
	int ret;

	ret = v4l2_subdev_disable_streams(csidev->s_subdev,
					  csidev->s_subdev_pad_nb, BIT_ULL(0));
	if (ret)
		return ret;

	/* Stop the VC0 */
	ret = stm32_csi_stop_vc(csidev, 0);
	if (ret)
		dev_err(csidev->dev, "Failed to stop VC0\n");

	stm32_csi_stop(csidev);

	return 0;
}

static int stm32_csi_enable_streams(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *state, u32 pad,
				    u64 streams_mask)
{
	struct stm32_csi_dev *csidev = to_csidev(sd);
	int ret;

	ret = stm32_csi_start(csidev, state);
	if (ret)
		return ret;

	/* Configure & start the VC0 */
	ret = stm32_csi_start_vc(csidev, state, 0);
	if (ret) {
		dev_err(csidev->dev, "Failed to start VC0\n");
		goto failed_start_vc;
	}

	ret = v4l2_subdev_enable_streams(csidev->s_subdev,
					 csidev->s_subdev_pad_nb, BIT_ULL(0));
	if (ret)
		goto failed_enable_streams;

	return 0;

failed_enable_streams:
	stm32_csi_stop_vc(csidev, 0);
failed_start_vc:
	stm32_csi_stop(csidev);
	return ret;
}

static int stm32_csi_init_state(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state)
{
	unsigned int i;

	for (i = 0; i < sd->entity.num_pads; i++)
		*v4l2_subdev_state_get_format(state, i) = fmt_default;

	return 0;
}

static int stm32_csi_enum_mbus_code(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *state,
				    struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(stm32_csi_formats))
		return -EINVAL;

	code->code = stm32_csi_formats[code->index].code;
	return 0;
}

static int stm32_csi_set_pad_format(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *state,
				    struct v4l2_subdev_format *format)
{
	struct stm32_csi_dev *csidev = to_csidev(sd);
	struct v4l2_mbus_framefmt *framefmt;
	const struct stm32_csi_fmts *fmt;

	fmt = stm32_csi_code_to_fmt(format->format.code);
	if (!fmt) {
		dev_dbg(csidev->dev, "Unsupported code %d, use default\n",
			format->format.code);
		format->format.code = fmt_default.code;
	}

	framefmt = v4l2_subdev_state_get_format(state, STM32_CSI_PAD_SINK);

	if (format->pad == STM32_CSI_PAD_SOURCE)
		format->format = *framefmt;
	else
		*framefmt = format->format;

	framefmt = v4l2_subdev_state_get_format(state, STM32_CSI_PAD_SOURCE);
	*framefmt = format->format;

	return 0;
}

static int stm32_csi_log_status(struct v4l2_subdev *sd)
{
	struct stm32_csi_dev *csidev = to_csidev(sd);
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&csidev->slock, flags);

	for (i = 0; i < STM32_CSI_NUM_SR0_EVENTS; i++) {
		if (csidev->sr0_counters[i])
			dev_info(csidev->dev, "%s events: %d\n",
				 stm32_csi_events_sr0[i].name,
				 csidev->sr0_counters[i]);
	}

	for (i = 0; i < STM32_CSI_NUM_SR1_EVENTS; i++) {
		if (csidev->sr1_counters[i])
			dev_info(csidev->dev, "%s events: %d\n",
				 stm32_csi_events_sr1[i].name,
				 csidev->sr1_counters[i]);
	}

	spin_unlock_irqrestore(&csidev->slock, flags);

	return 0;
}

static const struct v4l2_subdev_core_ops stm32_csi_core_ops = {
	.log_status = stm32_csi_log_status,
};

static const struct v4l2_subdev_video_ops stm32_csi_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops stm32_csi_pad_ops = {
	.enum_mbus_code = stm32_csi_enum_mbus_code,
	.set_fmt = stm32_csi_set_pad_format,
	.get_fmt = v4l2_subdev_get_fmt,
	.enable_streams = stm32_csi_enable_streams,
	.disable_streams = stm32_csi_disable_streams,
};

static const struct v4l2_subdev_ops stm32_csi_subdev_ops = {
	.core		= &stm32_csi_core_ops,
	.pad		= &stm32_csi_pad_ops,
	.video		= &stm32_csi_video_ops,
};

static const struct v4l2_subdev_internal_ops stm32_csi_subdev_internal_ops = {
	.init_state = stm32_csi_init_state,
};

static int stm32_csi_async_bound(struct v4l2_async_notifier *notifier,
				 struct v4l2_subdev *s_subdev,
				 struct v4l2_async_connection *asd)
{
	struct v4l2_subdev *sd = notifier->sd;
	struct stm32_csi_dev *csidev = to_csidev(sd);
	int remote_pad;

	remote_pad = media_entity_get_fwnode_pad(&s_subdev->entity,
						 s_subdev->fwnode,
						 MEDIA_PAD_FL_SOURCE);
	if (remote_pad < 0) {
		dev_err(csidev->dev, "Couldn't find output pad for subdev %s\n",
			s_subdev->name);
		return remote_pad;
	}

	csidev->s_subdev = s_subdev;
	csidev->s_subdev_pad_nb = remote_pad;

	return media_create_pad_link(&csidev->s_subdev->entity,
				     remote_pad, &csidev->sd.entity,
				     STM32_CSI_PAD_SINK,
				     MEDIA_LNK_FL_ENABLED |
				     MEDIA_LNK_FL_IMMUTABLE);
}

static const struct v4l2_async_notifier_operations stm32_csi_notifier_ops = {
	.bound		= stm32_csi_async_bound,
};

static irqreturn_t stm32_csi_irq_thread(int irq, void *arg)
{
	struct stm32_csi_dev *csidev = arg;
	unsigned long flags;
	u32 sr0, sr1;
	int i;

	sr0 = readl_relaxed(csidev->base + STM32_CSI_SR0);
	sr1 = readl_relaxed(csidev->base + STM32_CSI_SR1);

	/* Clear interrupt */
	writel_relaxed(sr0 & STM32_CSI_SR0_ERRORS,
		       csidev->base + STM32_CSI_FCR0);
	writel_relaxed(sr1 & STM32_CSI_SR1_ERRORS,
		       csidev->base + STM32_CSI_FCR1);

	spin_lock_irqsave(&csidev->slock, flags);

	for (i = 0; i < STM32_CSI_NUM_SR0_EVENTS; i++)
		if (sr0 & stm32_csi_events_sr0[i].mask)
			csidev->sr0_counters[i]++;

	for (i = 0; i < STM32_CSI_NUM_SR1_EVENTS; i++)
		if (sr1 & stm32_csi_events_sr1[i].mask)
			csidev->sr1_counters[i]++;

	spin_unlock_irqrestore(&csidev->slock, flags);

	return IRQ_HANDLED;
}

static int stm32_csi_get_resources(struct stm32_csi_dev *csidev,
				   struct platform_device *pdev)
{
	unsigned int i;
	int irq, ret;

	csidev->base = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(csidev->base))
		return dev_err_probe(&pdev->dev, PTR_ERR(csidev->base),
				     "Failed to ioremap resource\n");

	for (i = 0; i < STM32_CSI_CLK_NB; i++)
		csidev->clks[i].id = stm32_csi_clks_id[i];

	ret = devm_clk_bulk_get(&pdev->dev, STM32_CSI_CLK_NB,
				csidev->clks);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "Couldn't get clks\n");

	csidev->supplies[0].supply = "vdd";
	csidev->supplies[1].supply = "vdda18";
	ret = devm_regulator_bulk_get(&pdev->dev, ARRAY_SIZE(csidev->supplies),
				      csidev->supplies);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to request regulator vdd\n");

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					stm32_csi_irq_thread, IRQF_ONESHOT,
					dev_name(&pdev->dev), csidev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Unable to request irq");

	return 0;
}

static int stm32_csi_parse_dt(struct stm32_csi_dev *csidev)
{
	struct v4l2_fwnode_endpoint v4l2_ep = { .bus_type = V4L2_MBUS_CSI2_DPHY };
	struct v4l2_async_connection *asd;
	struct fwnode_handle *ep;
	int ret;

	/* Get bus characteristics from devicetree */
	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(csidev->dev), 0, 0,
					     FWNODE_GRAPH_ENDPOINT_NEXT);
	if (!ep) {
		dev_err(csidev->dev, "Could not find the endpoint\n");
		return -ENODEV;
	}

	ret = v4l2_fwnode_endpoint_parse(ep, &v4l2_ep);
	if (ret) {
		dev_err(csidev->dev, "Could not parse v4l2 endpoint\n");
		goto out;
	}

	csidev->num_lanes = v4l2_ep.bus.mipi_csi2.num_data_lanes;
	if (csidev->num_lanes > STM32_CSI_LANES_MAX) {
		dev_err(csidev->dev, "Unsupported number of data-lanes: %d\n",
			csidev->num_lanes);
		ret = -EINVAL;
		goto out;
	}

	memcpy(csidev->lanes, v4l2_ep.bus.mipi_csi2.data_lanes,
	       sizeof(csidev->lanes));

	v4l2_async_subdev_nf_init(&csidev->notifier, &csidev->sd);

	asd = v4l2_async_nf_add_fwnode_remote(&csidev->notifier, ep,
					      struct v4l2_async_connection);


	if (IS_ERR(asd)) {
		dev_err(csidev->dev, "Failed to add fwnode remote subdev\n");
		ret = PTR_ERR(asd);
		goto out;
	}

	csidev->notifier.ops = &stm32_csi_notifier_ops;

	ret = v4l2_async_nf_register(&csidev->notifier);
	if (ret) {
		dev_err(csidev->dev, "Failed to register notifier\n");
		v4l2_async_nf_cleanup(&csidev->notifier);
		goto out;
	}

out:
	fwnode_handle_put(ep);
	return ret;
}

static int stm32_csi_probe(struct platform_device *pdev)
{
	struct stm32_csi_dev *csidev;
	struct reset_control *rstc;
	int ret;

	csidev = devm_kzalloc(&pdev->dev, sizeof(*csidev), GFP_KERNEL);
	if (!csidev)
		return -ENOMEM;

	platform_set_drvdata(pdev, csidev);
	csidev->dev = &pdev->dev;

	spin_lock_init(&csidev->slock);

	ret = stm32_csi_get_resources(csidev, pdev);
	if (ret)
		return ret;

	ret = stm32_csi_parse_dt(csidev);
	if (ret)
		return ret;

	csidev->sd.owner = THIS_MODULE;
	csidev->sd.dev = &pdev->dev;
	csidev->sd.internal_ops = &stm32_csi_subdev_internal_ops;
	v4l2_subdev_init(&csidev->sd, &stm32_csi_subdev_ops);
	v4l2_set_subdevdata(&csidev->sd, &pdev->dev);
	snprintf(csidev->sd.name, sizeof(csidev->sd.name), "%s",
		 dev_name(&pdev->dev));

	/* Create our media pads */
	csidev->sd.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	csidev->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	csidev->pads[STM32_CSI_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	csidev->pads[STM32_CSI_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&csidev->sd.entity, STM32_CSI_PAD_MAX,
				     csidev->pads);
	if (ret)
		goto err_cleanup;

	ret = v4l2_subdev_init_finalize(&csidev->sd);
	if (ret < 0)
		goto err_cleanup;

	/* Reset device */
	rstc = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(rstc)) {
		ret = dev_err_probe(&pdev->dev, PTR_ERR(rstc),
				    "Couldn't get reset control\n");
		goto err_cleanup;
	}

	ret = reset_control_assert(rstc);
	if (ret) {
		ret = dev_err_probe(&pdev->dev, ret,
				    "Failed to assert the reset line\n");
		goto err_cleanup;
	}

	usleep_range(3000, 5000);

	ret = reset_control_deassert(rstc);
	if (ret) {
		ret = dev_err_probe(&pdev->dev, ret,
				    "Failed to deassert the reset line\n");
		goto err_cleanup;
	}

	pm_runtime_enable(&pdev->dev);

	ret = v4l2_async_register_subdev(&csidev->sd);
	if (ret < 0)
		goto err_cleanup;

	dev_info(&pdev->dev,
		 "Probed CSI with %u lanes\n", csidev->num_lanes);

	return 0;

err_cleanup:
	v4l2_async_nf_cleanup(&csidev->notifier);
	return ret;
}

static void stm32_csi_remove(struct platform_device *pdev)
{
	struct stm32_csi_dev *csidev = platform_get_drvdata(pdev);

	v4l2_async_unregister_subdev(&csidev->sd);

	pm_runtime_disable(&pdev->dev);
}

static int stm32_csi_runtime_suspend(struct device *dev)
{
	struct stm32_csi_dev *csidev = dev_get_drvdata(dev);
	int ret;

	clk_bulk_disable_unprepare(STM32_CSI_CLK_NB, csidev->clks);

	ret = regulator_bulk_disable(ARRAY_SIZE(csidev->supplies),
				     csidev->supplies);
	if (ret < 0)
		dev_err(dev, "cannot disable regulators %d\n", ret);

	return 0;
}

static int stm32_csi_runtime_resume(struct device *dev)
{
	struct stm32_csi_dev *csidev = dev_get_drvdata(dev);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(csidev->supplies),
				    csidev->supplies);
	if (ret)
		goto error_out;

	ret = clk_bulk_prepare_enable(STM32_CSI_CLK_NB, csidev->clks);
	if (ret)
		goto error_disable_supplies;

	return 0;

error_disable_supplies:
	ret = regulator_bulk_disable(ARRAY_SIZE(csidev->supplies), csidev->supplies);
	if (ret < 0)
		dev_err(dev, "cannot disable regulators %d\n", ret);
error_out:
	dev_err(csidev->dev, "Failed to resume: %d\n", ret);

	return ret;
}

static const struct of_device_id stm32_csi_of_table[] = {
	{ .compatible = "st,stm32mp25-csi", },
	{ /* end node */ },
};
MODULE_DEVICE_TABLE(of, stm32_csi_of_table);

static const struct dev_pm_ops stm32_csi_pm_ops = {
	RUNTIME_PM_OPS(stm32_csi_runtime_suspend,
		       stm32_csi_runtime_resume, NULL)
};

static struct platform_driver stm32_csi_driver = {
	.driver	= {
		.name = "stm32-csi",
		.of_match_table = stm32_csi_of_table,
		.pm = pm_ptr(&stm32_csi_pm_ops),
	},
	.probe	= stm32_csi_probe,
	.remove = stm32_csi_remove,
};

module_platform_driver(stm32_csi_driver);

MODULE_AUTHOR("Alain Volmat <alain.volmat@foss.st.com>");
MODULE_DESCRIPTION("STM32 CSI controller");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Tegra20-specific VI implementation
 *
 * Copyright (C) 2023 SKIDATA GmbH
 * Author: Luca Ceresoli <luca.ceresoli@bootlin.com>
 *
 * Copyright (c) 2025 Svyatoslav Ryhel <clamor95@gmail.com>
 * Copyright (c) 2025 Jonas Schwöbel <jonasschwoebel@yahoo.de>
 */

/*
 * This source file contains Tegra20 supported video formats,
 * VI and VIP SoC specific data, operations and registers accessors.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk/tegra.h>
#include <linux/delay.h>
#include <linux/host1x.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/pm_runtime.h>
#include <linux/tegra-mipi-cal.h>
#include <linux/v4l2-mediabus.h>

#include "vip.h"
#include "vi.h"

#define TEGRA_VI_SYNCPT_WAIT_TIMEOUT			msecs_to_jiffies(200)

#define TEGRA20_MIN_WIDTH	32U
#define TEGRA20_MAX_WIDTH	8190U
#define TEGRA20_MIN_HEIGHT	32U
#define TEGRA20_MAX_HEIGHT	8190U

/* Tegra20/Tegra30 has 2 outputs in VI */
enum tegra_vi_out {
	TEGRA_VI_OUT_1 = 0,
	TEGRA_VI_OUT_2 = 1,
};

/* --------------------------------------------------------------------------
 * Registers
 */

#define TEGRA_VI_CONT_SYNCPT_OUT(n)			(0x0060 + (n) * 4)
#define       VI_CONT_SYNCPT_OUT_CONTINUOUS_SYNCPT	BIT(8)
#define       VI_CONT_SYNCPT_OUT_SYNCPT_IDX_SFT		0

#define TEGRA_VI_CONT_SYNCPT_CSI_PP_FRAME_START(n)	(0x0070 + (n) * 8)
#define TEGRA_VI_CONT_SYNCPT_CSI_PP_FRAME_END(n)	(0x0074 + (n) * 8)

#define TEGRA_VI_VI_INPUT_CONTROL			0x0088
#define       VI_INPUT_FIELD_DETECT			BIT(27)
#define       VI_INPUT_BT656				BIT(25)
#define       VI_INPUT_YUV_INPUT_FORMAT_SFT		8  /* bits [9:8] */
#define       VI_INPUT_YUV_INPUT_FORMAT_UYVY		(0 << VI_INPUT_YUV_INPUT_FORMAT_SFT)
#define       VI_INPUT_YUV_INPUT_FORMAT_VYUY		BIT(VI_INPUT_YUV_INPUT_FORMAT_SFT)
#define       VI_INPUT_YUV_INPUT_FORMAT_YUYV		(2 << VI_INPUT_YUV_INPUT_FORMAT_SFT)
#define       VI_INPUT_YUV_INPUT_FORMAT_YVYU		(3 << VI_INPUT_YUV_INPUT_FORMAT_SFT)
#define       VI_INPUT_INPUT_FORMAT_SFT			2  /* bits [5:2] */
#define       VI_INPUT_INPUT_FORMAT_YUV422		(0 << VI_INPUT_INPUT_FORMAT_SFT)
#define       VI_INPUT_INPUT_FORMAT_BAYER		(2 << VI_INPUT_INPUT_FORMAT_SFT)
#define       VI_INPUT_VIP_INPUT_ENABLE			BIT(1)

#define TEGRA_VI_VI_CORE_CONTROL			0x008c
#define       VI_VI_CORE_CONTROL_PLANAR_CONV_IN_SEL_EXT	BIT(31)
#define       VI_VI_CORE_CONTROL_CSC_INPUT_SEL_EXT	BIT(30)
#define       VI_VI_CORE_CONTROL_INPUT_TO_ALT_MUX_SFT	27
#define       VI_VI_CORE_CONTROL_INPUT_TO_CORE_EXT_SFT	24
#define       VI_VI_CORE_CONTROL_OUTPUT_TO_ISP_EXT_SFT	21
#define       VI_VI_CORE_CONTROL_ISP_HOST_STALL_OFF	BIT(20)
#define       VI_VI_CORE_CONTROL_V_DOWNSCALING		BIT(19)
#define       VI_VI_CORE_CONTROL_V_AVERAGING		BIT(18)
#define       VI_VI_CORE_CONTROL_H_DOWNSCALING		BIT(17)
#define       VI_VI_CORE_CONTROL_H_AVERAGING		BIT(16)
#define       VI_VI_CORE_CONTROL_CSC_INPUT_SEL		BIT(11)
#define       VI_VI_CORE_CONTROL_PLANAR_CONV_INPUT_SEL	BIT(10)
#define       VI_VI_CORE_CONTROL_INPUT_TO_CORE_SFT	8
#define       VI_VI_CORE_CONTROL_ISP_DOWNSAMPLE_SFT	5
#define       VI_VI_CORE_CONTROL_OUTPUT_TO_EPP_SFT	2
#define       VI_VI_CORE_CONTROL_OUTPUT_TO_ISP_SFT	0

#define TEGRA_VI_VI_OUTPUT_CONTROL(n)			(0x0090 + (n) * 4)
#define       VI_OUTPUT_FORMAT_EXT			BIT(22)
#define       VI_OUTPUT_V_DIRECTION			BIT(20)
#define       VI_OUTPUT_H_DIRECTION			BIT(19)
#define       VI_OUTPUT_YUV_OUTPUT_FORMAT_SFT		17
#define       VI_OUTPUT_YUV_OUTPUT_FORMAT_UYVY		(0 << VI_OUTPUT_YUV_OUTPUT_FORMAT_SFT)
#define       VI_OUTPUT_YUV_OUTPUT_FORMAT_VYUY		BIT(VI_OUTPUT_YUV_OUTPUT_FORMAT_SFT)
#define       VI_OUTPUT_YUV_OUTPUT_FORMAT_YUYV		(2 << VI_OUTPUT_YUV_OUTPUT_FORMAT_SFT)
#define       VI_OUTPUT_YUV_OUTPUT_FORMAT_YVYU		(3 << VI_OUTPUT_YUV_OUTPUT_FORMAT_SFT)
#define       VI_OUTPUT_OUTPUT_BYTE_SWAP		BIT(16)
#define       VI_OUTPUT_LAST_PIXEL_DUPLICATION		BIT(8)
#define       VI_OUTPUT_OUTPUT_FORMAT_SFT		0
#define       VI_OUTPUT_OUTPUT_FORMAT_YUV422POST	(3 << VI_OUTPUT_OUTPUT_FORMAT_SFT)
#define       VI_OUTPUT_OUTPUT_FORMAT_YUV420PLANAR	(6 << VI_OUTPUT_OUTPUT_FORMAT_SFT)
/* TEGRA_VI_OUT_2 supported formats */
#define       VI_OUTPUT_OUTPUT_FORMAT_CSI_PPA_BAYER	(7 << VI_OUTPUT_OUTPUT_FORMAT_SFT)
#define       VI_OUTPUT_OUTPUT_FORMAT_CSI_PPB_BAYER	(8 << VI_OUTPUT_OUTPUT_FORMAT_SFT)
#define       VI_OUTPUT_OUTPUT_FORMAT_VIP_BAYER_DIRECT	(9 << VI_OUTPUT_OUTPUT_FORMAT_SFT)

#define TEGRA_VI_VIP_H_ACTIVE				0x00a4
#define       VI_VIP_H_ACTIVE_PERIOD_SFT		16 /* active pixels/line, must be even */
#define       VI_VIP_H_ACTIVE_START_SFT			0

#define TEGRA_VI_VIP_V_ACTIVE				0x00a8
#define       VI_VIP_V_ACTIVE_PERIOD_SFT		16 /* active lines */
#define       VI_VIP_V_ACTIVE_START_SFT			0

#define TEGRA_VI_VB0_START_ADDRESS(n)			(0x00c4 + (n) * 44)
#define TEGRA_VI_VB0_BASE_ADDRESS(n)			(0x00c8 + (n) * 44)
#define TEGRA_VI_VB0_START_ADDRESS_U			0x00cc
#define TEGRA_VI_VB0_BASE_ADDRESS_U			0x00d0
#define TEGRA_VI_VB0_START_ADDRESS_V			0x00d4
#define TEGRA_VI_VB0_BASE_ADDRESS_V			0x00d8

#define TEGRA_VI_OUTPUT_FRAME_SIZE(n)			(0x00e0 + (n) * 24)
#define       VI_OUTPUT_FRAME_HEIGHT_SFT		16
#define       VI_OUTPUT_FRAME_WIDTH_SFT			0

#define TEGRA_VI_VB0_COUNT(n)				(0x00e4 + (n) * 24)

#define TEGRA_VI_VB0_SIZE(n)				(0x00e8 + (n) * 24)
#define       VI_VB0_SIZE_V_SFT				16
#define       VI_VB0_SIZE_H_SFT				0

#define TEGRA_VI_VB0_BUFFER_STRIDE(n)			(0x00ec + (n) * 24)
#define       VI_VB0_BUFFER_STRIDE_CHROMA_SFT		30
#define       VI_VB0_BUFFER_STRIDE_LUMA_SFT		0

#define TEGRA_VI_H_LPF_CONTROL				0x0108
#define       VI_H_LPF_CONTROL_CHROMA_SFT		16
#define       VI_H_LPF_CONTROL_LUMA_SFT			0

#define TEGRA_VI_H_DOWNSCALE_CONTROL			0x010c
#define TEGRA_VI_V_DOWNSCALE_CONTROL			0x0110

#define TEGRA_VI_VIP_INPUT_STATUS			0x0144

#define TEGRA_VI_VI_DATA_INPUT_CONTROL			0x0168
#define       VI_DATA_INPUT_SFT				0 /* [11:0] = mask pin inputs to VI core */

#define TEGRA_VI_PIN_INPUT_ENABLE			0x016c
#define       VI_PIN_INPUT_VSYNC			BIT(14)
#define       VI_PIN_INPUT_HSYNC			BIT(13)
#define       VI_PIN_INPUT_VD_SFT			0 /* [11:0] = data bin N input enable */

#define TEGRA_VI_PIN_INVERSION				0x0174
#define       VI_PIN_INVERSION_VSYNC_ACTIVE_HIGH	BIT(1)
#define       VI_PIN_INVERSION_HSYNC_ACTIVE_HIGH	BIT(0)

#define TEGRA_VI_CAMERA_CONTROL				0x01a0
#define       VI_CAMERA_CONTROL_STOP_CAPTURE		BIT(2)
#define       VI_CAMERA_CONTROL_TEST_MODE		BIT(1)
#define       VI_CAMERA_CONTROL_VIP_ENABLE		BIT(0)

#define TEGRA_VI_VI_ENABLE(n)				(0x01a4 + (n) * 4)
#define       VI_VI_ENABLE_SW_FLOW_CONTROL_OUT1		BIT(1)
#define       VI_VI_ENABLE_FIRST_OUTPUT_TO_MEM_DISABLE	BIT(0)

#define TEGRA_VI_VI_RAISE				0x01ac
#define       VI_VI_RAISE_ON_EDGE			BIT(0)

#define TEGRA_VI_CSI_PP_RAISE_FRAME_START(n)		(0x01d8 + (n) * 8)
#define TEGRA_VI_CSI_PP_RAISE_FRAME_END(n)		(0x01dc + (n) * 8)
#define TEGRA_VI_CSI_PP_H_ACTIVE(n)			(0x01e8 + (n) * 8)
#define TEGRA_VI_CSI_PP_V_ACTIVE(n)			(0x01ec + (n) * 8)

/* Tegra20 CSI registers: Starts from 0x800, offset 0x0 */
#define TEGRA_CSI_VI_INPUT_STREAM_CONTROL		0x0000
#define TEGRA_CSI_HOST_INPUT_STREAM_CONTROL		0x0008
#define TEGRA_CSI_INPUT_STREAM_CONTROL(n)		(0x0010 + (n) * 0x2c)
#define       CSI_SKIP_PACKET_THRESHOLD(n)		(((n) & 0xff) << 16)
#define TEGRA_CSI_PIXEL_STREAM_CONTROL0(n)		(0x0018 + (n) * 0x2c)
#define       CSI_PP_PAD_FRAME_PAD0S			(0 << 28)
#define       CSI_PP_PAD_FRAME_PAD1S			(1 << 28)
#define       CSI_PP_PAD_FRAME_NOPAD			(2 << 28)
#define       CSI_PP_HEADER_EC_ENABLE			BIT(27)
#define       CSI_PP_PAD_SHORT_LINE_PAD0S		(0 << 24)
#define       CSI_PP_PAD_SHORT_LINE_PAD1S		(1 << 24)
#define       CSI_PP_PAD_SHORT_LINE_NOPAD		(2 << 24)
#define       CSI_PP_EMBEDDED_DATA_EMBEDDED		BIT(20)
#define       CSI_PP_OUTPUT_FORMAT_ARBITRARY		(0 << 16)
#define       CSI_PP_OUTPUT_FORMAT_PIXEL		(1 << 16)
#define       CSI_PP_OUTPUT_FORMAT_PIXEL_REP		(2 << 16)
#define       CSI_PP_OUTPUT_FORMAT_STORE		(3 << 16)
#define       CSI_PP_VIRTUAL_CHANNEL_ID(n)		(((n) - 1) << 14)
#define       CSI_PP_DATA_TYPE(n)			((n) << 8)
#define       CSI_PP_CRC_CHECK_ENABLE			BIT(7)
#define       CSI_PP_WORD_COUNT_HEADER			BIT(6)
#define       CSI_PP_DATA_IDENTIFIER_ENABLE		BIT(5)
#define       CSI_PP_PACKET_HEADER_SENT			BIT(4)
#define TEGRA_CSI_PIXEL_STREAM_CONTROL1(n)		(0x001c + (n) * 0x2c)
#define TEGRA_CSI_PIXEL_STREAM_WORD_COUNT(n)		(0x0020 + (n) * 0x2c)
#define TEGRA_CSI_PIXEL_STREAM_GAP(n)			(0x0024 + (n) * 0x2c)
#define       CSI_PP_FRAME_MIN_GAP(n)			(((n) & 0xffff) << 16)
#define       CSI_PP_LINE_MIN_GAP(n)			(((n) & 0xffff))
#define TEGRA_CSI_PIXEL_STREAM_PP_COMMAND(n)		(0x0028 + (n) * 0x2c)
#define       CSI_PP_START_MARKER_FRAME_MAX(n)		(((n) & 0xf) << 12)
#define       CSI_PP_START_MARKER_FRAME_MIN(n)		(((n) & 0xf) << 8)
#define       CSI_PP_VSYNC_START_MARKER			BIT(4)
#define       CSI_PP_SINGLE_SHOT			BIT(2)
#define       CSI_PP_NOP				0
#define       CSI_PP_ENABLE				1
#define       CSI_PP_DISABLE				2
#define       CSI_PP_RESET				3
#define TEGRA_CSI_PHY_CIL_COMMAND			0x0068
#define       CSI_A_PHY_CIL_NOP				0x0
#define       CSI_A_PHY_CIL_ENABLE			0x1
#define       CSI_A_PHY_CIL_DISABLE			0x2
#define       CSI_A_PHY_CIL_ENABLE_MASK			0x3
#define       CSI_B_PHY_CIL_NOP				(0x0 << 16)
#define       CSI_B_PHY_CIL_ENABLE			(0x1 << 16)
#define       CSI_B_PHY_CIL_DISABLE			(0x2 << 16)
#define       CSI_B_PHY_CIL_ENABLE_MASK			(0x3 << 16)
#define TEGRA_CSI_PHY_CIL_CONTROL0(n)			(0x006c + (n) * 4)
#define       CSI_CONTINUOUS_CLOCK_MODE_ENABLE		BIT(5)
#define TEGRA_CSI_CSI_PIXEL_PARSER_STATUS		0x0078
#define TEGRA_CSI_CSI_CIL_STATUS			0x007c
#define       CSI_MIPI_AUTO_CAL_DONE			BIT(15)
#define TEGRA_CSI_CSI_PIXEL_PARSER_INTERRUPT_MASK	0x0080
#define TEGRA_CSI_CSI_CIL_INTERRUPT_MASK		0x0084
#define TEGRA_CSI_CSI_READONLY_STATUS			0x0088
#define TEGRA_CSI_ESCAPE_MODE_COMMAND			0x008c
#define TEGRA_CSI_ESCAPE_MODE_DATA			0x0090
#define TEGRA_CSI_CIL_PAD_CONFIG0(n)			(0x0094 + (n) * 8)
#define TEGRA_CSI_CIL_PAD_CONFIG1(n)			(0x0098 + (n) * 8)
#define TEGRA_CSI_CIL_PAD_CONFIG			0x00a4
#define TEGRA_CSI_CILA_MIPI_CAL_CONFIG			0x00a8
#define TEGRA_CSI_CILB_MIPI_CAL_CONFIG			0x00ac
#define       CSI_CIL_MIPI_CAL_STARTCAL			BIT(31)
#define       CSI_CIL_MIPI_CAL_OVERIDE_A		BIT(30)
#define       CSI_CIL_MIPI_CAL_OVERIDE_B		BIT(30)
#define       CSI_CIL_MIPI_CAL_NOISE_FLT(n)		(((n) & 0xf) << 26)
#define       CSI_CIL_MIPI_CAL_PRESCALE(n)		(((n) & 0x3) << 24)
#define       CSI_CIL_MIPI_CAL_SEL_A			BIT(21)
#define       CSI_CIL_MIPI_CAL_SEL_B			BIT(21)
#define       CSI_CIL_MIPI_CAL_HSPDOS(n)		(((n) & 0x1f) << 16)
#define       CSI_CIL_MIPI_CAL_HSPUOS(n)		(((n) & 0x1f) << 8)
#define       CSI_CIL_MIPI_CAL_TERMOS(n)		(((n) & 0x1f))
#define TEGRA_CSI_CIL_MIPI_CAL_STATUS			0x00b0
#define TEGRA_CSI_CLKEN_OVERRIDE			0x00b4
#define TEGRA_CSI_DEBUG_CONTROL				0x00b8
#define       CSI_DEBUG_CONTROL_DEBUG_EN_ENABLED	BIT(0)
#define       CSI_DEBUG_CONTROL_CLR_DBG_CNT_0		BIT(4)
#define       CSI_DEBUG_CONTROL_CLR_DBG_CNT_1		BIT(5)
#define       CSI_DEBUG_CONTROL_CLR_DBG_CNT_2		BIT(6)
#define       CSI_DEBUG_CONTROL_DBG_CNT_SEL(n, v)	((v) << (8 + 8 * (n)))
#define TEGRA_CSI_DEBUG_COUNTER(n)			(0x00bc + (n) * 4)
#define TEGRA_CSI_PIXEL_STREAM_EXPECTED_FRAME(n)	(0x00c8 + (n) * 4)
#define       CSI_PP_EXP_FRAME_HEIGHT(n)		(((n) & 0x1fff) << 16)
#define       CSI_PP_MAX_CLOCKS(n)			(((n) & 0xfff) << 4)
#define       CSI_PP_LINE_TIMEOUT_ENABLE		BIT(0)
#define TEGRA_CSI_DSI_MIPI_CAL_CONFIG			0x00d0
#define TEGRA_CSI_MIPIBIAS_PAD_CONFIG			0x00d4
#define       CSI_PAD_DRIV_DN_REF(n)			(((n) & 0x7) << 16)
#define       CSI_PAD_DRIV_UP_REF(n)			(((n) & 0x7) << 8)
#define       CSI_PAD_TERM_REF(n)			(((n) & 0x7) << 0)
#define TEGRA_CSI_CSI_CILA_STATUS			0x00d8
#define TEGRA_CSI_CSI_CILB_STATUS			0x00dc

/* --------------------------------------------------------------------------
 * Read and Write helpers
 */

static void tegra20_vi_write(struct tegra_vi_channel *chan, unsigned int addr, u32 val)
{
	writel(val, chan->vi->iomem + addr);
}

static int __maybe_unused tegra20_vi_read(struct tegra_vi_channel *chan, unsigned int addr)
{
	return readl(chan->vi->iomem + addr);
}

static void tegra20_csi_write(struct tegra_csi_channel *csi_chan, unsigned int addr, u32 val)
{
	writel(val, csi_chan->csi->iomem + addr);
}

static int __maybe_unused tegra20_csi_read(struct tegra_csi_channel *csi_chan, unsigned int addr)
{
	return readl(csi_chan->csi->iomem + addr);
}

static void tegra20_mipi_write(struct tegra_csi *csi, unsigned int addr, u32 val)
{
	writel(val, csi->iomem + addr);
}

static int __maybe_unused tegra20_mipi_read(struct tegra_csi *csi, unsigned int addr)
{
	return readl(csi->iomem + addr);
}

/* --------------------------------------------------------------------------
 * VI
 */

/*
 * Get the main input format (YUV/RGB...) and the YUV variant as values to
 * be written into registers for the current VI input mbus code.
 */
static void tegra20_vi_get_input_formats(struct tegra_vi_channel *chan,
					 unsigned int *main_input_format,
					 unsigned int *yuv_input_format)
{
	unsigned int input_mbus_code = chan->fmtinfo->code;

	(*main_input_format) = VI_INPUT_INPUT_FORMAT_YUV422;
	(*yuv_input_format) = VI_INPUT_YUV_INPUT_FORMAT_UYVY;

	switch (input_mbus_code) {
	case MEDIA_BUS_FMT_UYVY8_2X8:
		(*yuv_input_format) = VI_INPUT_YUV_INPUT_FORMAT_UYVY;
		break;
	case MEDIA_BUS_FMT_VYUY8_2X8:
		(*yuv_input_format) = VI_INPUT_YUV_INPUT_FORMAT_VYUY;
		break;
	case MEDIA_BUS_FMT_YUYV8_2X8:
		(*yuv_input_format) = VI_INPUT_YUV_INPUT_FORMAT_YUYV;
		break;
	case MEDIA_BUS_FMT_YVYU8_2X8:
		(*yuv_input_format) = VI_INPUT_YUV_INPUT_FORMAT_YVYU;
		break;
	/* RAW8 */
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SRGGB8_1X8:
	/* RAW10 */
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SRGGB10_1X10:
		(*main_input_format) = VI_INPUT_INPUT_FORMAT_BAYER;
		break;
	}
}

/*
 * Get the main output format (YUV/RGB...) and the YUV variant as values to
 * be written into registers for the current VI output pixel format.
 */
static void tegra20_vi_get_output_formats(struct tegra_vi_channel *chan,
					  unsigned int *main_output_format,
					  unsigned int *yuv_output_format)
{
	u32 output_fourcc = chan->format.pixelformat;

	/* Default to YUV422 non-planar (U8Y8V8Y8) after downscaling */
	(*main_output_format) = VI_OUTPUT_OUTPUT_FORMAT_YUV422POST;
	(*yuv_output_format) = VI_OUTPUT_YUV_OUTPUT_FORMAT_UYVY;

	switch (output_fourcc) {
	case V4L2_PIX_FMT_UYVY:
		(*yuv_output_format) = VI_OUTPUT_YUV_OUTPUT_FORMAT_UYVY;
		break;
	case V4L2_PIX_FMT_VYUY:
		(*yuv_output_format) = VI_OUTPUT_YUV_OUTPUT_FORMAT_VYUY;
		break;
	case V4L2_PIX_FMT_YUYV:
		(*yuv_output_format) = VI_OUTPUT_YUV_OUTPUT_FORMAT_YUYV;
		break;
	case V4L2_PIX_FMT_YVYU:
		(*yuv_output_format) = VI_OUTPUT_YUV_OUTPUT_FORMAT_YVYU;
		break;
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		(*main_output_format) = VI_OUTPUT_OUTPUT_FORMAT_YUV420PLANAR;
		break;
	/* RAW8 */
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SRGGB8:
	/* RAW10 */
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_SGRBG10:
	case V4L2_PIX_FMT_SRGGB10:
		(*main_output_format) = VI_OUTPUT_OUTPUT_FORMAT_VIP_BAYER_DIRECT;
		break;
	}
}

/*
 * Make the VI accessible (needed on Tegra20).
 *
 * This function writes an unknown bit into an unknown register. The code
 * comes from a downstream 3.1 kernel that has a working VIP driver for
 * Tegra20, and removing it makes the VI completely unaccessible. It should
 * be rewritten and possibly moved elsewhere, but the appropriate location
 * and implementation is unknown due to a total lack of documentation.
 */
static int tegra20_vi_enable(struct tegra_vi *vi, bool on)
{
	/* from arch/arm/mach-tegra/iomap.h */
	const phys_addr_t TEGRA_APB_MISC_BASE = 0x70000000;
	const unsigned long reg_offset = 0x42c;
	void __iomem *apb_misc;
	u32 val;

	apb_misc = ioremap(TEGRA_APB_MISC_BASE, PAGE_SIZE);
	if (!apb_misc)
		apb_misc = ERR_PTR(-ENOENT);
	if (IS_ERR(apb_misc))
		return dev_err_probe(vi->dev, PTR_ERR(apb_misc), "cannot access APB_MISC");

	val = readl(apb_misc + reg_offset);
	val &= ~BIT(0);
	val |= on ? BIT(0) : 0;
	writel(val, apb_misc + reg_offset);
	iounmap(apb_misc);

	return 0;
}

static int tegra20_channel_host1x_syncpt_init(struct tegra_vi_channel *chan)
{
	struct tegra_vi *vi = chan->vi;
	struct host1x_syncpt *out_sp, *fs_sp;

	out_sp = host1x_syncpt_request(&vi->client, HOST1X_SYNCPT_CLIENT_MANAGED);
	if (!out_sp)
		return dev_err_probe(vi->dev, -EBUSY, "failed to request mw ack syncpoint\n");

	chan->mw_ack_sp[0] = out_sp;

	fs_sp = host1x_syncpt_request(&vi->client, HOST1X_SYNCPT_CLIENT_MANAGED);
	if (!fs_sp)
		return dev_err_probe(vi->dev, -EBUSY, "failed to request frame start syncpoint\n");

	chan->frame_start_sp[0] = fs_sp;

	return 0;
}

static void tegra20_channel_host1x_syncpt_free(struct tegra_vi_channel *chan)
{
	host1x_syncpt_put(chan->mw_ack_sp[0]);
	host1x_syncpt_put(chan->frame_start_sp[0]);
}

static void tegra20_fmt_align(struct v4l2_pix_format *pix, unsigned int bpp)
{
	pix->width  = clamp(pix->width,  TEGRA20_MIN_WIDTH,  TEGRA20_MAX_WIDTH);
	pix->height = clamp(pix->height, TEGRA20_MIN_HEIGHT, TEGRA20_MAX_HEIGHT);

	pix->bytesperline = roundup(pix->width, 8) * bpp;
	pix->sizeimage = pix->bytesperline * pix->height;

	switch (pix->pixelformat) {
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		pix->sizeimage = pix->sizeimage * 3 / 2;
		break;
	}
}

/*
 * Compute buffer offsets once per stream so that
 * tegra20_channel_vi_buffer_setup() only has to do very simple maths for
 * each buffer.
 */
static void tegra20_channel_queue_setup(struct tegra_vi_channel *chan)
{
	unsigned int stride = chan->format.bytesperline;
	unsigned int height = chan->format.height;

	chan->start_offset = 0;

	switch (chan->format.pixelformat) {
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
	/* RAW8 */
	case V4L2_PIX_FMT_SRGGB8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SBGGR8:
	/* RAW10 */
	case V4L2_PIX_FMT_SRGGB10:
	case V4L2_PIX_FMT_SGRBG10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_SBGGR10:
		if (chan->vflip)
			chan->start_offset += stride * (height - 1);
		if (chan->hflip)
			chan->start_offset += stride - 1;
		break;

	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		chan->addr_offset_u = stride * height;
		chan->addr_offset_v = chan->addr_offset_u + stride * height / 4;

		/* For YVU420, we swap the locations of the U and V planes. */
		if (chan->format.pixelformat == V4L2_PIX_FMT_YVU420)
			swap(chan->addr_offset_u, chan->addr_offset_v);

		chan->start_offset_u = chan->addr_offset_u;
		chan->start_offset_v = chan->addr_offset_v;

		if (chan->vflip) {
			chan->start_offset   += stride * (height - 1);
			chan->start_offset_u += (stride / 2) * ((height / 2) - 1);
			chan->start_offset_v += (stride / 2) * ((height / 2) - 1);
		}
		if (chan->hflip) {
			chan->start_offset   += stride - 1;
			chan->start_offset_u += (stride / 2) - 1;
			chan->start_offset_v += (stride / 2) - 1;
		}
		break;
	}
}

static void release_buffer(struct tegra_vi_channel *chan,
			   struct tegra_channel_buffer *buf,
			   enum vb2_buffer_state state)
{
	struct vb2_v4l2_buffer *vb = &buf->buf;

	vb->sequence = chan->sequence++;
	vb->field = V4L2_FIELD_NONE;
	vb->vb2_buf.timestamp = ktime_get_ns();
	vb2_buffer_done(&vb->vb2_buf, state);
}

static void tegra20_channel_vi_buffer_setup(struct tegra_vi_channel *chan,
					    struct tegra_channel_buffer *buf)
{
	dma_addr_t base = buf->addr;

	switch (chan->fmtinfo->fourcc) {
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		tegra20_vi_write(chan, TEGRA_VI_VB0_BASE_ADDRESS_U,  base + chan->addr_offset_u);
		tegra20_vi_write(chan, TEGRA_VI_VB0_START_ADDRESS_U, base + chan->start_offset_u);
		tegra20_vi_write(chan, TEGRA_VI_VB0_BASE_ADDRESS_V,  base + chan->addr_offset_v);
		tegra20_vi_write(chan, TEGRA_VI_VB0_START_ADDRESS_V, base + chan->start_offset_v);
		fallthrough;

	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
		tegra20_vi_write(chan, TEGRA_VI_VB0_BASE_ADDRESS(TEGRA_VI_OUT_1),  base);
		tegra20_vi_write(chan, TEGRA_VI_VB0_START_ADDRESS(TEGRA_VI_OUT_1), base + chan->start_offset);
		break;
	/* RAW8 */
	case V4L2_PIX_FMT_SRGGB8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SBGGR8:
	/* RAW10 */
	case V4L2_PIX_FMT_SRGGB10:
	case V4L2_PIX_FMT_SGRBG10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_SBGGR10:
		tegra20_vi_write(chan, TEGRA_VI_VB0_BASE_ADDRESS(TEGRA_VI_OUT_2),  base);
		tegra20_vi_write(chan, TEGRA_VI_VB0_START_ADDRESS(TEGRA_VI_OUT_2), base + chan->start_offset);
		break;
	}
}

static int tegra20_channel_capture_frame(struct tegra_vi_channel *chan,
					 struct tegra_channel_buffer *buf,
					 struct tegra_csi_channel *csi_chan)
{
	u32 val;
	int err;

	tegra20_channel_vi_buffer_setup(chan, buf);

	if (csi_chan) {
		u32 port = csi_chan->csi_port_nums[0] & 1;

		tegra20_csi_write(csi_chan, TEGRA_CSI_PIXEL_STREAM_PP_COMMAND(port),
				  CSI_PP_START_MARKER_FRAME_MAX(0xf) |
				  CSI_PP_SINGLE_SHOT | CSI_PP_ENABLE);

		/*
		 * ERESTARTSYS workaround for syncpoints is used because host1x_syncpt_wait
		 * is unconditionally interruptible. This is not an issue with single shots
		 * or low resolution capture, but -ERESTARTSYS occurs quite often with high
		 * resolution or high framerate captures and if not addressed here will
		 * cause capture to fail entirely.
		 *
		 * TODO: once uninterruptible version of host1x_syncpt_wait is available,
		 * host1x_syncpt_wait should be swapped and ERESTARTSYS workaround can be
		 * removed.
		 */

		val = host1x_syncpt_read(chan->frame_start_sp[0]);
		do {
			err = host1x_syncpt_wait(chan->frame_start_sp[0],
						 val + 1, TEGRA_VI_SYNCPT_WAIT_TIMEOUT, NULL);
		} while (err == -ERESTARTSYS);

		if (err) {
			if (err != -ERESTARTSYS)
				dev_err_ratelimited(&chan->video.dev,
						    "frame start syncpt timeout: %d\n", err);

			tegra20_csi_write(csi_chan, TEGRA_CSI_PIXEL_STREAM_PP_COMMAND(port),
					  CSI_PP_START_MARKER_FRAME_MAX(0xf) | CSI_PP_RESET);
			goto exit;
		}

		tegra20_csi_write(csi_chan, TEGRA_CSI_PIXEL_STREAM_PP_COMMAND(port),
				  CSI_PP_START_MARKER_FRAME_MAX(0xf) |
				  CSI_PP_DISABLE);
	} else {
		tegra20_vi_write(chan, TEGRA_VI_CAMERA_CONTROL, VI_CAMERA_CONTROL_VIP_ENABLE);
	}

	val = host1x_syncpt_read(chan->mw_ack_sp[0]);
	do {
		err = host1x_syncpt_wait(chan->mw_ack_sp[0], val + 1,
					 TEGRA_VI_SYNCPT_WAIT_TIMEOUT, NULL);
	} while (err == -ERESTARTSYS);

	if (err) {
		if (err != -ERESTARTSYS)
			dev_err_ratelimited(&chan->video.dev, "mw ack syncpt timeout: %d\n", err);
		goto exit;
	}

	if (!csi_chan)
		tegra20_vi_write(chan, TEGRA_VI_CAMERA_CONTROL,
				 VI_CAMERA_CONTROL_STOP_CAPTURE | VI_CAMERA_CONTROL_VIP_ENABLE);

exit:
	release_buffer(chan, buf, VB2_BUF_STATE_DONE);

	return err;
}

static int tegra20_chan_capture_kthread_start(void *data)
{
	struct tegra_vi_channel *chan = data;
	struct tegra_channel_buffer *buf;
	struct v4l2_subdev *csi_subdev = NULL;
	struct tegra_csi_channel *csi_chan = NULL;
	unsigned int retries = 0;
	int err = 0;

	csi_subdev = tegra_channel_get_remote_csi_subdev(chan);
	if (csi_subdev)
		csi_chan = to_csi_chan(csi_subdev);

	while (1) {
		/*
		 * Source is not streaming if error is non-zero.
		 * So, do not dequeue buffers on error and let the thread sleep
		 * till kthread stop signal is received.
		 */
		wait_event_interruptible(chan->start_wait,
					 kthread_should_stop() ||
					 (!list_empty(&chan->capture) && !err));

		if (kthread_should_stop())
			break;

		/* dequeue the buffer and start capture */
		spin_lock(&chan->start_lock);
		if (list_empty(&chan->capture)) {
			spin_unlock(&chan->start_lock);
			continue;
		}

		buf = list_first_entry(&chan->capture, struct tegra_channel_buffer, queue);
		list_del_init(&buf->queue);
		spin_unlock(&chan->start_lock);

		err = tegra20_channel_capture_frame(chan, buf, csi_chan);
		if (!err) {
			retries = 0;
			continue;
		}

		if (retries++ > chan->syncpt_timeout_retry)
			vb2_queue_error(&chan->queue);
		else
			err = 0;
	}

	return 0;
}

static void tegra20_camera_capture_setup(struct tegra_vi_channel *chan)
{
	u32 output_fourcc = chan->format.pixelformat;
	u32 data_type = chan->fmtinfo->img_dt;
	int width  = chan->format.width;
	int height = chan->format.height;
	int stride_l = chan->format.bytesperline * height;
	int stride_c = (output_fourcc == V4L2_PIX_FMT_YUV420 ||
			output_fourcc == V4L2_PIX_FMT_YVU420) ? 1 : 0;
	enum tegra_vi_out output_channel = (data_type == TEGRA_IMAGE_DT_RAW8 ||
					    data_type == TEGRA_IMAGE_DT_RAW10) ?
					    TEGRA_VI_OUT_2 : TEGRA_VI_OUT_1;

	/* Set up frame size */
	tegra20_vi_write(chan, TEGRA_VI_OUTPUT_FRAME_SIZE(output_channel),
			 height << VI_OUTPUT_FRAME_HEIGHT_SFT |
			 width  << VI_OUTPUT_FRAME_WIDTH_SFT);

	/* First output memory enabled */
	tegra20_vi_write(chan, TEGRA_VI_VI_ENABLE(output_channel), 0);

	/* Set the number of frames in the buffer */
	tegra20_vi_write(chan, TEGRA_VI_VB0_COUNT(output_channel), 1);

	/* Set up buffer frame size */
	tegra20_vi_write(chan, TEGRA_VI_VB0_SIZE(output_channel),
			 height << VI_VB0_SIZE_V_SFT |
			 width  << VI_VB0_SIZE_H_SFT);

	tegra20_vi_write(chan, TEGRA_VI_VB0_BUFFER_STRIDE(output_channel),
			 stride_l << VI_VB0_BUFFER_STRIDE_LUMA_SFT |
			 stride_c << VI_VB0_BUFFER_STRIDE_CHROMA_SFT);

	tegra20_vi_write(chan, TEGRA_VI_VI_ENABLE(output_channel), 0);
}

static int tegra20_vi_start_streaming(struct vb2_queue *vq, u32 count)
{
	struct tegra_vi_channel *chan = vb2_get_drv_priv(vq);
	struct media_pipeline *pipe = &chan->video.pipe;
	int err;

	err = video_device_pipeline_start(&chan->video, pipe);
	if (err)
		goto error_pipeline_start;

	/*
	 * Set up low pass filter.  Use 0x240 for chromaticity and 0x240
	 * for luminance, which is the default and means not to touch
	 * anything.
	 */
	tegra20_vi_write(chan, TEGRA_VI_H_LPF_CONTROL,
			 0x0240 << VI_H_LPF_CONTROL_LUMA_SFT |
			 0x0240 << VI_H_LPF_CONTROL_CHROMA_SFT);

	/* Set up raise-on-edge, so we get an interrupt on end of frame. */
	tegra20_vi_write(chan, TEGRA_VI_VI_RAISE, VI_VI_RAISE_ON_EDGE);

	err = tegra_channel_set_stream(chan, true);
	if (err)
		goto error_set_stream;

	tegra20_camera_capture_setup(chan);

	chan->sequence = 0;

	chan->kthread_start_capture = kthread_run(tegra20_chan_capture_kthread_start,
						  chan, "%s:0", chan->video.name);
	if (IS_ERR(chan->kthread_start_capture)) {
		err = PTR_ERR(chan->kthread_start_capture);
		chan->kthread_start_capture = NULL;
		dev_err_probe(&chan->video.dev, err, "failed to run capture kthread\n");
		goto error_kthread_start;
	}

	return 0;

error_kthread_start:
	tegra_channel_set_stream(chan, false);
error_set_stream:
	video_device_pipeline_stop(&chan->video);
error_pipeline_start:
	tegra_channel_release_buffers(chan, VB2_BUF_STATE_QUEUED);

	return err;
}

static void tegra20_vi_stop_streaming(struct vb2_queue *vq)
{
	struct tegra_vi_channel *chan = vb2_get_drv_priv(vq);

	if (chan->kthread_start_capture) {
		kthread_stop(chan->kthread_start_capture);
		chan->kthread_start_capture = NULL;
	}

	tegra_channel_release_buffers(chan, VB2_BUF_STATE_ERROR);
	tegra_channel_set_stream(chan, false);
	video_device_pipeline_stop(&chan->video);
}

static const struct tegra_vi_ops tegra20_vi_ops = {
	.vi_enable = tegra20_vi_enable,
	.channel_host1x_syncpt_init = tegra20_channel_host1x_syncpt_init,
	.channel_host1x_syncpt_free = tegra20_channel_host1x_syncpt_free,
	.vi_fmt_align = tegra20_fmt_align,
	.channel_queue_setup = tegra20_channel_queue_setup,
	.vi_start_streaming = tegra20_vi_start_streaming,
	.vi_stop_streaming = tegra20_vi_stop_streaming,
};

#define TEGRA20_VIDEO_FMT(DATA_TYPE, BIT_WIDTH, MBUS_CODE, BPP, FOURCC)	\
{									\
	.img_dt		= TEGRA_IMAGE_DT_##DATA_TYPE,			\
	.bit_width	= BIT_WIDTH,					\
	.code		= MEDIA_BUS_FMT_##MBUS_CODE,			\
	.bpp		= BPP,						\
	.fourcc		= V4L2_PIX_FMT_##FOURCC,			\
}

static const struct tegra_video_format tegra20_video_formats[] = {
	/* YUV422 */
	TEGRA20_VIDEO_FMT(YUV422_8, 16, UYVY8_2X8, 2, UYVY),
	TEGRA20_VIDEO_FMT(YUV422_8, 16, VYUY8_2X8, 2, VYUY),
	TEGRA20_VIDEO_FMT(YUV422_8, 16, YUYV8_2X8, 2, YUYV),
	TEGRA20_VIDEO_FMT(YUV422_8, 16, YVYU8_2X8, 2, YVYU),
	TEGRA20_VIDEO_FMT(YUV422_8, 16, UYVY8_1X16, 2, UYVY),
	TEGRA20_VIDEO_FMT(YUV422_8, 16, VYUY8_1X16, 2, VYUY),
	TEGRA20_VIDEO_FMT(YUV422_8, 16, YUYV8_1X16, 2, YUYV),
	TEGRA20_VIDEO_FMT(YUV422_8, 16, YVYU8_1X16, 2, YVYU),
	/* YUV420P */
	TEGRA20_VIDEO_FMT(YUV422_8, 16, UYVY8_2X8, 1, YUV420),
	TEGRA20_VIDEO_FMT(YUV422_8, 16, UYVY8_2X8, 1, YVU420),
	TEGRA20_VIDEO_FMT(YUV422_8, 16, UYVY8_1X16, 1, YUV420),
	TEGRA20_VIDEO_FMT(YUV422_8, 16, UYVY8_1X16, 1, YVU420),
	/* RAW 8 */
	TEGRA20_VIDEO_FMT(RAW8, 8, SRGGB8_1X8, 2, SRGGB8),
	TEGRA20_VIDEO_FMT(RAW8, 8, SGRBG8_1X8, 2, SGRBG8),
	TEGRA20_VIDEO_FMT(RAW8, 8, SGBRG8_1X8, 2, SGBRG8),
	TEGRA20_VIDEO_FMT(RAW8, 8, SBGGR8_1X8, 2, SBGGR8),
	/* RAW 10 */
	TEGRA20_VIDEO_FMT(RAW10, 10, SRGGB10_1X10, 2, SRGGB10),
	TEGRA20_VIDEO_FMT(RAW10, 10, SGRBG10_1X10, 2, SGRBG10),
	TEGRA20_VIDEO_FMT(RAW10, 10, SGBRG10_1X10, 2, SGBRG10),
	TEGRA20_VIDEO_FMT(RAW10, 10, SBGGR10_1X10, 2, SBGGR10),
};

const struct tegra_vi_soc tegra20_vi_soc = {
	.video_formats = tegra20_video_formats,
	.nformats = ARRAY_SIZE(tegra20_video_formats),
	.default_video_format = &tegra20_video_formats[0],
	.ops = &tegra20_vi_ops,
	.hw_revision = 1,
	.vi_max_channels = 2, /* TEGRA_VI_OUT_1 and TEGRA_VI_OUT_2 */
	.vi_max_clk_hz = 450000000,
	.has_h_v_flip = true,
};

/* --------------------------------------------------------------------------
 * MIPI Calibration
 */
static int tegra20_start_pad_calibration(struct tegra_mipi_device *mipi)
{
	struct tegra_csi *csi = platform_get_drvdata(mipi->pdev);
	unsigned int port = mipi->pads;
	u32 value;
	int ret;

	guard(mutex)(&csi->mipi_lock);

	ret = pm_runtime_resume_and_get(csi->dev);
	if (ret < 0) {
		dev_err(csi->dev, "failed to get runtime PM: %d\n", ret);
		return ret;
	}

	tegra20_mipi_write(csi, TEGRA_CSI_DSI_MIPI_CAL_CONFIG,
			   CSI_CIL_MIPI_CAL_HSPDOS(4) |
			   CSI_CIL_MIPI_CAL_HSPUOS(3) |
			   CSI_CIL_MIPI_CAL_TERMOS(0));
	tegra20_mipi_write(csi, TEGRA_CSI_MIPIBIAS_PAD_CONFIG,
			   CSI_PAD_DRIV_DN_REF(5) |
			   CSI_PAD_DRIV_UP_REF(7) |
			   CSI_PAD_TERM_REF(0));

	/* CSI B */
	value = CSI_CIL_MIPI_CAL_HSPDOS(0) |
		CSI_CIL_MIPI_CAL_HSPUOS(0) |
		CSI_CIL_MIPI_CAL_TERMOS(4);

	if (port == PORT_B)
		value |= CSI_CIL_MIPI_CAL_SEL_B;

	tegra20_mipi_write(csi, TEGRA_CSI_CILB_MIPI_CAL_CONFIG, value);

	/* CSI A */
	value = CSI_CIL_MIPI_CAL_STARTCAL |
		CSI_CIL_MIPI_CAL_NOISE_FLT(0xa) |
		CSI_CIL_MIPI_CAL_PRESCALE(0x2) |
		CSI_CIL_MIPI_CAL_HSPDOS(0) |
		CSI_CIL_MIPI_CAL_HSPUOS(0) |
		CSI_CIL_MIPI_CAL_TERMOS(4);

	if (port == PORT_A)
		value |= CSI_CIL_MIPI_CAL_SEL_A;

	tegra20_mipi_write(csi, TEGRA_CSI_CILA_MIPI_CAL_CONFIG, value);

	tegra20_mipi_write(csi, TEGRA_CSI_CIL_PAD_CONFIG, 0);

	return 0;
}

static int tegra20_finish_pad_calibration(struct tegra_mipi_device *mipi)
{
	struct tegra_csi *csi = platform_get_drvdata(mipi->pdev);
	void __iomem *cil_status_reg = csi->iomem + TEGRA_CSI_CSI_CIL_STATUS;
	unsigned int port = mipi->pads;
	u32 value, pp = 0, cil = 0;
	int ret;

	/* This part is only for CSI */
	if (port > PORT_B) {
		pm_runtime_put(csi->dev);

		return 0;
	}

	guard(mutex)(&csi->mipi_lock);

	ret = readl_relaxed_poll_timeout(cil_status_reg, value,
					 value & CSI_MIPI_AUTO_CAL_DONE, 50, 250000);
	if (ret < 0) {
		dev_warn(csi->dev, "MIPI calibration timeout!\n");
		goto exit;
	}

	/* clear status */
	tegra20_mipi_write(csi, TEGRA_CSI_CSI_CIL_STATUS, value);
	ret = readl_relaxed_poll_timeout(cil_status_reg, value,
					 !(value & CSI_MIPI_AUTO_CAL_DONE), 50, 250000);
	if (ret < 0) {
		dev_warn(csi->dev, "MIPI calibration status timeout!\n");
		goto exit;
	}

	pp = tegra20_mipi_read(csi, TEGRA_CSI_CSI_PIXEL_PARSER_STATUS);
	cil = tegra20_mipi_read(csi, TEGRA_CSI_CSI_CIL_STATUS);
	if (pp | cil) {
		dev_warn(csi->dev, "Calibration status not been cleared!\n");
		ret = -EINVAL;
		goto exit;
	}

exit:
	tegra20_mipi_write(csi, TEGRA_CSI_CSI_CIL_STATUS, pp);

	/* un-select to avoid interference with DSI */
	tegra20_mipi_write(csi, TEGRA_CSI_CILB_MIPI_CAL_CONFIG,
			   CSI_CIL_MIPI_CAL_HSPDOS(0) |
			   CSI_CIL_MIPI_CAL_HSPUOS(0) |
			   CSI_CIL_MIPI_CAL_TERMOS(4));

	tegra20_mipi_write(csi, TEGRA_CSI_CILA_MIPI_CAL_CONFIG,
			   CSI_CIL_MIPI_CAL_NOISE_FLT(0xa) |
			   CSI_CIL_MIPI_CAL_PRESCALE(0x2) |
			   CSI_CIL_MIPI_CAL_HSPDOS(0) |
			   CSI_CIL_MIPI_CAL_HSPUOS(0) |
			   CSI_CIL_MIPI_CAL_TERMOS(4));

	pm_runtime_put(csi->dev);

	return ret;
}

static const struct tegra_mipi_ops tegra20_mipi_ops = {
	.start_calibration = tegra20_start_pad_calibration,
	.finish_calibration = tegra20_finish_pad_calibration,
};

/* --------------------------------------------------------------------------
 * CSI
 */
static void tegra20_csi_capture_clean(struct tegra_csi_channel *csi_chan)
{
	tegra20_csi_write(csi_chan, TEGRA_CSI_VI_INPUT_STREAM_CONTROL, 0);
	tegra20_csi_write(csi_chan, TEGRA_CSI_HOST_INPUT_STREAM_CONTROL, 0);

	tegra20_csi_write(csi_chan, TEGRA_CSI_CSI_PIXEL_PARSER_STATUS, 0);
	tegra20_csi_write(csi_chan, TEGRA_CSI_CSI_CIL_STATUS, 0);
	tegra20_csi_write(csi_chan, TEGRA_CSI_CSI_PIXEL_PARSER_INTERRUPT_MASK, 0);
	tegra20_csi_write(csi_chan, TEGRA_CSI_CSI_CIL_INTERRUPT_MASK, 0);
	tegra20_csi_write(csi_chan, TEGRA_CSI_CSI_READONLY_STATUS, 0);
	tegra20_csi_write(csi_chan, TEGRA_CSI_ESCAPE_MODE_COMMAND, 0);
	tegra20_csi_write(csi_chan, TEGRA_CSI_ESCAPE_MODE_DATA, 0);

	tegra20_csi_write(csi_chan, TEGRA_CSI_CIL_PAD_CONFIG, 0);
	tegra20_csi_write(csi_chan, TEGRA_CSI_CIL_MIPI_CAL_STATUS, 0);
	tegra20_csi_write(csi_chan, TEGRA_CSI_CLKEN_OVERRIDE, 0);

	tegra20_csi_write(csi_chan, TEGRA_CSI_DEBUG_CONTROL,
			  CSI_DEBUG_CONTROL_CLR_DBG_CNT_0 |
			  CSI_DEBUG_CONTROL_CLR_DBG_CNT_1 |
			  CSI_DEBUG_CONTROL_CLR_DBG_CNT_2);
}

static int tegra20_csi_port_start_streaming(struct tegra_csi_channel *csi_chan,
					    u8 portno)
{
	struct tegra_vi_channel *vi_chan = v4l2_get_subdev_hostdata(&csi_chan->subdev);
	int width  = vi_chan->format.width;
	int height = vi_chan->format.height;
	u32 data_type = vi_chan->fmtinfo->img_dt;
	u32 word_count = (width * vi_chan->fmtinfo->bit_width) / 8;
	enum tegra_vi_out output_channel = TEGRA_VI_OUT_1;

	unsigned int main_output_format, yuv_output_format;
	unsigned int port = portno & 1;
	u32 value;

	tegra20_vi_get_output_formats(vi_chan, &main_output_format, &yuv_output_format);

	switch (data_type) {
	case TEGRA_IMAGE_DT_RAW8:
	case TEGRA_IMAGE_DT_RAW10:
		output_channel = TEGRA_VI_OUT_2;
		if (port == PORT_A)
			main_output_format = VI_OUTPUT_OUTPUT_FORMAT_CSI_PPA_BAYER;
		else
			main_output_format = VI_OUTPUT_OUTPUT_FORMAT_CSI_PPB_BAYER;
		break;
	}

	tegra20_csi_capture_clean(csi_chan);

	/* CSI port cleanup */
	tegra20_csi_write(csi_chan, TEGRA_CSI_INPUT_STREAM_CONTROL(port), 0);
	tegra20_csi_write(csi_chan, TEGRA_CSI_PIXEL_STREAM_CONTROL0(port), 0);
	tegra20_csi_write(csi_chan, TEGRA_CSI_PIXEL_STREAM_CONTROL1(port), 0);
	tegra20_csi_write(csi_chan, TEGRA_CSI_PIXEL_STREAM_WORD_COUNT(port), 0);
	tegra20_csi_write(csi_chan, TEGRA_CSI_PIXEL_STREAM_GAP(port), 0);
	tegra20_csi_write(csi_chan, TEGRA_CSI_PIXEL_STREAM_PP_COMMAND(port), 0);
	tegra20_csi_write(csi_chan, TEGRA_CSI_PIXEL_STREAM_EXPECTED_FRAME(port), 0);
	tegra20_csi_write(csi_chan, TEGRA_CSI_PHY_CIL_CONTROL0(port), 0);
	tegra20_csi_write(csi_chan, TEGRA_CSI_CIL_PAD_CONFIG0(port), 0);
	tegra20_csi_write(csi_chan, TEGRA_CSI_CIL_PAD_CONFIG1(port), 0);

	tegra20_vi_write(vi_chan, TEGRA_VI_VI_CORE_CONTROL, BIT(25 + port)); /* CSI_PP_YUV422 */

	tegra20_vi_write(vi_chan, TEGRA_VI_H_DOWNSCALE_CONTROL, BIT(2 + port)); /* CSI_PP */
	tegra20_vi_write(vi_chan, TEGRA_VI_V_DOWNSCALE_CONTROL, BIT(2 + port)); /* CSI_PP */

	tegra20_vi_write(vi_chan, TEGRA_VI_CSI_PP_H_ACTIVE(port), width << 16);
	tegra20_vi_write(vi_chan, TEGRA_VI_CSI_PP_V_ACTIVE(port), height << 16);

	tegra20_csi_write(csi_chan, TEGRA_CSI_PIXEL_STREAM_CONTROL1(port), 0x1);

	tegra20_csi_write(csi_chan, TEGRA_CSI_PIXEL_STREAM_WORD_COUNT(port), word_count);
	tegra20_csi_write(csi_chan, TEGRA_CSI_PIXEL_STREAM_GAP(port),
			  CSI_PP_FRAME_MIN_GAP(0x14)); /* 14 vi clks between frames */

	tegra20_csi_write(csi_chan, TEGRA_CSI_PIXEL_STREAM_EXPECTED_FRAME(port),
			  CSI_PP_EXP_FRAME_HEIGHT(height) |
			  CSI_PP_MAX_CLOCKS(0x300) | /* wait 0x300 vi clks for timeout */
			  CSI_PP_LINE_TIMEOUT_ENABLE);

	tegra20_csi_write(csi_chan, TEGRA_CSI_PIXEL_STREAM_CONTROL0(port),
			  CSI_PP_OUTPUT_FORMAT_PIXEL |
			  CSI_PP_DATA_TYPE(data_type) |
			  CSI_PP_CRC_CHECK_ENABLE |
			  CSI_PP_WORD_COUNT_HEADER |
			  CSI_PP_DATA_IDENTIFIER_ENABLE |
			  CSI_PP_PACKET_HEADER_SENT |
			  port);

	tegra20_csi_write(csi_chan, TEGRA_CSI_INPUT_STREAM_CONTROL(port),
			  CSI_SKIP_PACKET_THRESHOLD(0x3f) |
			  (csi_chan->numlanes - 1));

	tegra20_csi_write(csi_chan, TEGRA_CSI_PHY_CIL_CONTROL0(port),
			  CSI_CONTINUOUS_CLOCK_MODE_ENABLE |
			  0x5); /* Clock settle time */

	tegra20_vi_write(vi_chan, TEGRA_VI_CONT_SYNCPT_CSI_PP_FRAME_START(port),
			 VI_CONT_SYNCPT_OUT_CONTINUOUS_SYNCPT |
			 host1x_syncpt_id(vi_chan->frame_start_sp[0])
			 << VI_CONT_SYNCPT_OUT_SYNCPT_IDX_SFT);

	tegra20_vi_write(vi_chan, TEGRA_VI_CONT_SYNCPT_OUT(output_channel),
			 VI_CONT_SYNCPT_OUT_CONTINUOUS_SYNCPT |
			 host1x_syncpt_id(vi_chan->mw_ack_sp[0])
			 << VI_CONT_SYNCPT_OUT_SYNCPT_IDX_SFT);

	value = (port == PORT_A) ? CSI_A_PHY_CIL_ENABLE | CSI_B_PHY_CIL_DISABLE :
		CSI_B_PHY_CIL_ENABLE | CSI_A_PHY_CIL_DISABLE;
	tegra20_csi_write(csi_chan, TEGRA_CSI_PHY_CIL_COMMAND, value);

	tegra20_csi_write(csi_chan, TEGRA_CSI_PIXEL_STREAM_PP_COMMAND(port),
			  CSI_PP_START_MARKER_FRAME_MAX(0xf) |
			  CSI_PP_DISABLE);

	tegra20_vi_write(vi_chan, TEGRA_VI_VI_OUTPUT_CONTROL(output_channel),
			 (vi_chan->vflip ? VI_OUTPUT_V_DIRECTION : 0) |
			 (vi_chan->hflip ? VI_OUTPUT_H_DIRECTION : 0) |
			 yuv_output_format | main_output_format);

	return 0;
};

static void tegra20_csi_port_stop_streaming(struct tegra_csi_channel *csi_chan, u8 portno)
{
	struct tegra_csi *csi = csi_chan->csi;
	unsigned int port = portno & 1;
	u32 value;

	value = tegra20_csi_read(csi_chan, TEGRA_CSI_CSI_PIXEL_PARSER_STATUS);
	dev_dbg(csi->dev, "TEGRA_CSI_CSI_PIXEL_PARSER_STATUS 0x%08x\n", value);
	tegra20_csi_write(csi_chan, TEGRA_CSI_CSI_PIXEL_PARSER_STATUS, value);

	value = tegra20_csi_read(csi_chan, TEGRA_CSI_CSI_CIL_STATUS);
	dev_dbg(csi->dev, "TEGRA_CSI_CSI_CIL_STATUS 0x%08x\n", value);
	tegra20_csi_write(csi_chan, TEGRA_CSI_CSI_CIL_STATUS, value);

	tegra20_csi_write(csi_chan, TEGRA_CSI_PIXEL_STREAM_PP_COMMAND(port),
			  CSI_PP_START_MARKER_FRAME_MAX(0xf) |
			  CSI_PP_DISABLE);

	if (csi_chan->numlanes == 4) {
		tegra20_csi_write(csi_chan, TEGRA_CSI_PHY_CIL_COMMAND,
				  CSI_A_PHY_CIL_DISABLE | CSI_B_PHY_CIL_DISABLE);
	} else {
		value = (port == PORT_A) ? CSI_A_PHY_CIL_DISABLE | CSI_B_PHY_CIL_NOP :
			CSI_B_PHY_CIL_DISABLE | CSI_A_PHY_CIL_NOP;
		tegra20_csi_write(csi_chan, TEGRA_CSI_PHY_CIL_COMMAND, value);
	}
}

static int tegra20_csi_start_streaming(struct tegra_csi_channel *csi_chan)
{
	u8 *portnos = csi_chan->csi_port_nums;
	int ret, i;

	for (i = 0; i < csi_chan->numgangports; i++) {
		ret = tegra20_csi_port_start_streaming(csi_chan, portnos[i]);
		if (ret)
			goto stream_start_fail;
	}

	return 0;

stream_start_fail:
	for (i = i - 1; i >= 0; i--)
		tegra20_csi_port_stop_streaming(csi_chan, portnos[i]);

	return ret;
}

static void tegra20_csi_stop_streaming(struct tegra_csi_channel *csi_chan)
{
	u8 *portnos = csi_chan->csi_port_nums;
	int i;

	for (i = 0; i < csi_chan->numgangports; i++)
		tegra20_csi_port_stop_streaming(csi_chan, portnos[i]);
}

static const struct tegra_csi_ops tegra20_csi_ops = {
	.csi_start_streaming = tegra20_csi_start_streaming,
	.csi_stop_streaming = tegra20_csi_stop_streaming,
};

static const char * const tegra20_csi_clks[] = {
	NULL,
};

const struct tegra_csi_soc tegra20_csi_soc = {
	.ops = &tegra20_csi_ops,
	.mipi_ops = &tegra20_mipi_ops,
	.csi_max_channels = 2, /* CSI-A and CSI-B */
	.clk_names = tegra20_csi_clks,
	.num_clks = ARRAY_SIZE(tegra20_csi_clks),
};

static const char * const tegra30_csi_clks[] = {
	"csi",
	"csia-pad",
	"csib-pad",
};

const struct tegra_csi_soc tegra30_csi_soc = {
	.ops = &tegra20_csi_ops,
	.mipi_ops = &tegra20_mipi_ops,
	.csi_max_channels = 2, /* CSI-A and CSI-B */
	.clk_names = tegra30_csi_clks,
	.num_clks = ARRAY_SIZE(tegra30_csi_clks),
};

/* --------------------------------------------------------------------------
 * VIP
 */

/*
 * VIP-specific configuration for stream start.
 *
 * Whatever is common among VIP and CSI is done by the VI component (see
 * tegra20_vi_start_streaming()). Here we do what is VIP-specific.
 */
static int tegra20_vip_start_streaming(struct tegra_vip_channel *vip_chan)
{
	struct tegra_vi_channel *vi_chan = v4l2_get_subdev_hostdata(&vip_chan->subdev);
	u32 data_type = vi_chan->fmtinfo->img_dt;
	int width  = vi_chan->format.width;
	int height = vi_chan->format.height;
	enum tegra_vi_out output_channel = (data_type == TEGRA_IMAGE_DT_RAW8 ||
					    data_type == TEGRA_IMAGE_DT_RAW10) ?
					    TEGRA_VI_OUT_2 : TEGRA_VI_OUT_1;
	unsigned int main_input_format, yuv_input_format;
	unsigned int main_output_format, yuv_output_format;

	tegra20_vi_get_input_formats(vi_chan, &main_input_format, &yuv_input_format);
	tegra20_vi_get_output_formats(vi_chan, &main_output_format, &yuv_output_format);

	tegra20_vi_write(vi_chan, TEGRA_VI_VI_CORE_CONTROL, 0);

	tegra20_vi_write(vi_chan, TEGRA_VI_VI_INPUT_CONTROL,
			 VI_INPUT_VIP_INPUT_ENABLE | main_input_format | yuv_input_format);

	tegra20_vi_write(vi_chan, TEGRA_VI_V_DOWNSCALE_CONTROL, 0);
	tegra20_vi_write(vi_chan, TEGRA_VI_H_DOWNSCALE_CONTROL, 0);

	tegra20_vi_write(vi_chan, TEGRA_VI_VIP_V_ACTIVE, height << VI_VIP_V_ACTIVE_PERIOD_SFT);
	tegra20_vi_write(vi_chan, TEGRA_VI_VIP_H_ACTIVE,
			 roundup(width, 2) << VI_VIP_H_ACTIVE_PERIOD_SFT);

	/*
	 * For VIP, D9..D2 is mapped to the video decoder's P7..P0.
	 * Disable/mask out the other Dn wires. When not in BT656
	 * mode we also need the V/H sync.
	 */
	tegra20_vi_write(vi_chan, TEGRA_VI_PIN_INPUT_ENABLE,
			 GENMASK(9, 2) << VI_PIN_INPUT_VD_SFT |
			 VI_PIN_INPUT_HSYNC | VI_PIN_INPUT_VSYNC);
	tegra20_vi_write(vi_chan, TEGRA_VI_VI_DATA_INPUT_CONTROL,
			 GENMASK(9, 2) << VI_DATA_INPUT_SFT);
	tegra20_vi_write(vi_chan, TEGRA_VI_PIN_INVERSION, 0);

	tegra20_vi_write(vi_chan, TEGRA_VI_CONT_SYNCPT_OUT(output_channel),
			 VI_CONT_SYNCPT_OUT_CONTINUOUS_SYNCPT |
			 host1x_syncpt_id(vi_chan->mw_ack_sp[0])
			 << VI_CONT_SYNCPT_OUT_SYNCPT_IDX_SFT);

	tegra20_vi_write(vi_chan, TEGRA_VI_CAMERA_CONTROL, VI_CAMERA_CONTROL_STOP_CAPTURE);

	tegra20_vi_write(vi_chan, TEGRA_VI_VI_OUTPUT_CONTROL(output_channel),
			 (vi_chan->vflip ? VI_OUTPUT_V_DIRECTION : 0) |
			 (vi_chan->hflip ? VI_OUTPUT_H_DIRECTION : 0) |
			  yuv_output_format | main_output_format);

	return 0;
}

static const struct tegra_vip_ops tegra20_vip_ops = {
	.vip_start_streaming = tegra20_vip_start_streaming,
};

const struct tegra_vip_soc tegra20_vip_soc = {
	.ops = &tegra20_vip_ops,
};

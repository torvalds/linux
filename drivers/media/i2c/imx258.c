// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Intel Corporation

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <asm/unaligned.h>

#define IMX258_REG_MODE_SELECT		CCI_REG8(0x0100)
#define IMX258_MODE_STANDBY		0x00
#define IMX258_MODE_STREAMING		0x01

#define IMX258_REG_RESET		CCI_REG8(0x0103)

/* Chip ID */
#define IMX258_REG_CHIP_ID		CCI_REG16(0x0016)
#define IMX258_CHIP_ID			0x0258

/* V_TIMING internal */
#define IMX258_VTS_30FPS		0x0c50
#define IMX258_VTS_30FPS_2K		0x0638
#define IMX258_VTS_30FPS_VGA		0x034c
#define IMX258_VTS_MAX			65525

/* HBLANK control - read only */
#define IMX258_PPL_DEFAULT		5352

/* Exposure control */
#define IMX258_REG_EXPOSURE		CCI_REG16(0x0202)
#define IMX258_EXPOSURE_OFFSET		10
#define IMX258_EXPOSURE_MIN		4
#define IMX258_EXPOSURE_STEP		1
#define IMX258_EXPOSURE_DEFAULT		0x640
#define IMX258_EXPOSURE_MAX		(IMX258_VTS_MAX - IMX258_EXPOSURE_OFFSET)

/* Analog gain control */
#define IMX258_REG_ANALOG_GAIN		CCI_REG16(0x0204)
#define IMX258_ANA_GAIN_MIN		0
#define IMX258_ANA_GAIN_MAX		480
#define IMX258_ANA_GAIN_STEP		1
#define IMX258_ANA_GAIN_DEFAULT		0x0

/* Digital gain control */
#define IMX258_REG_GR_DIGITAL_GAIN	CCI_REG16(0x020e)
#define IMX258_REG_R_DIGITAL_GAIN	CCI_REG16(0x0210)
#define IMX258_REG_B_DIGITAL_GAIN	CCI_REG16(0x0212)
#define IMX258_REG_GB_DIGITAL_GAIN	CCI_REG16(0x0214)
#define IMX258_DGTL_GAIN_MIN		0
#define IMX258_DGTL_GAIN_MAX		4096	/* Max = 0xFFF */
#define IMX258_DGTL_GAIN_DEFAULT	1024
#define IMX258_DGTL_GAIN_STEP		1

/* HDR control */
#define IMX258_REG_HDR			CCI_REG8(0x0220)
#define IMX258_HDR_ON			BIT(0)
#define IMX258_REG_HDR_RATIO		CCI_REG8(0x0222)
#define IMX258_HDR_RATIO_MIN		0
#define IMX258_HDR_RATIO_MAX		5
#define IMX258_HDR_RATIO_STEP		1
#define IMX258_HDR_RATIO_DEFAULT	0x0

/* Test Pattern Control */
#define IMX258_REG_TEST_PATTERN		CCI_REG16(0x0600)

#define IMX258_CLK_BLANK_STOP		CCI_REG8(0x4040)

/* Orientation */
#define REG_MIRROR_FLIP_CONTROL		CCI_REG8(0x0101)
#define REG_CONFIG_MIRROR_HFLIP		0x01
#define REG_CONFIG_MIRROR_VFLIP		0x02

/* IMX258 native and active pixel array size. */
#define IMX258_NATIVE_WIDTH		4224U
#define IMX258_NATIVE_HEIGHT		3192U
#define IMX258_PIXEL_ARRAY_LEFT		8U
#define IMX258_PIXEL_ARRAY_TOP		16U
#define IMX258_PIXEL_ARRAY_WIDTH	4208U
#define IMX258_PIXEL_ARRAY_HEIGHT	3120U

/* regs */
#define IMX258_REG_PLL_MULT_DRIV                  CCI_REG8(0x0310)
#define IMX258_REG_IVTPXCK_DIV                    CCI_REG8(0x0301)
#define IMX258_REG_IVTSYCK_DIV                    CCI_REG8(0x0303)
#define IMX258_REG_PREPLLCK_VT_DIV                CCI_REG8(0x0305)
#define IMX258_REG_IOPPXCK_DIV                    CCI_REG8(0x0309)
#define IMX258_REG_IOPSYCK_DIV                    CCI_REG8(0x030b)
#define IMX258_REG_PREPLLCK_OP_DIV                CCI_REG8(0x030d)
#define IMX258_REG_PHASE_PIX_OUTEN                CCI_REG8(0x3030)
#define IMX258_REG_PDPIX_DATA_RATE                CCI_REG8(0x3032)
#define IMX258_REG_SCALE_MODE                     CCI_REG8(0x0401)
#define IMX258_REG_SCALE_MODE_EXT                 CCI_REG8(0x3038)
#define IMX258_REG_AF_WINDOW_MODE                 CCI_REG8(0x7bcd)
#define IMX258_REG_FRM_LENGTH_CTL                 CCI_REG8(0x0350)
#define IMX258_REG_CSI_LANE_MODE                  CCI_REG8(0x0114)
#define IMX258_REG_X_EVN_INC                      CCI_REG8(0x0381)
#define IMX258_REG_X_ODD_INC                      CCI_REG8(0x0383)
#define IMX258_REG_Y_EVN_INC                      CCI_REG8(0x0385)
#define IMX258_REG_Y_ODD_INC                      CCI_REG8(0x0387)
#define IMX258_REG_BINNING_MODE                   CCI_REG8(0x0900)
#define IMX258_REG_BINNING_TYPE_V                 CCI_REG8(0x0901)
#define IMX258_REG_FORCE_FD_SUM                   CCI_REG8(0x300d)
#define IMX258_REG_DIG_CROP_X_OFFSET              CCI_REG16(0x0408)
#define IMX258_REG_DIG_CROP_Y_OFFSET              CCI_REG16(0x040a)
#define IMX258_REG_DIG_CROP_IMAGE_WIDTH           CCI_REG16(0x040c)
#define IMX258_REG_DIG_CROP_IMAGE_HEIGHT          CCI_REG16(0x040e)
#define IMX258_REG_SCALE_M                        CCI_REG16(0x0404)
#define IMX258_REG_X_OUT_SIZE                     CCI_REG16(0x034c)
#define IMX258_REG_Y_OUT_SIZE                     CCI_REG16(0x034e)
#define IMX258_REG_X_ADD_STA                      CCI_REG16(0x0344)
#define IMX258_REG_Y_ADD_STA                      CCI_REG16(0x0346)
#define IMX258_REG_X_ADD_END                      CCI_REG16(0x0348)
#define IMX258_REG_Y_ADD_END                      CCI_REG16(0x034a)
#define IMX258_REG_EXCK_FREQ                      CCI_REG16(0x0136)
#define IMX258_REG_CSI_DT_FMT                     CCI_REG16(0x0112)
#define IMX258_REG_LINE_LENGTH_PCK                CCI_REG16(0x0342)
#define IMX258_REG_SCALE_M_EXT                    CCI_REG16(0x303a)
#define IMX258_REG_FRM_LENGTH_LINES               CCI_REG16(0x0340)
#define IMX258_REG_FINE_INTEG_TIME                CCI_REG8(0x0200)
#define IMX258_REG_PLL_IVT_MPY                    CCI_REG16(0x0306)
#define IMX258_REG_PLL_IOP_MPY                    CCI_REG16(0x030e)
#define IMX258_REG_REQ_LINK_BIT_RATE_MBPS_H       CCI_REG16(0x0820)
#define IMX258_REG_REQ_LINK_BIT_RATE_MBPS_L       CCI_REG16(0x0822)

struct imx258_reg_list {
	u32 num_of_regs;
	const struct cci_reg_sequence *regs;
};

struct imx258_link_cfg {
	unsigned int lf_to_pix_rate_factor;
	struct imx258_reg_list reg_list;
};

enum {
	IMX258_2_LANE_MODE,
	IMX258_4_LANE_MODE,
	IMX258_LANE_CONFIGS,
};

/* Link frequency config */
struct imx258_link_freq_config {
	u32 pixels_per_line;

	/* Configuration for this link frequency / num lanes selection */
	struct imx258_link_cfg link_cfg[IMX258_LANE_CONFIGS];
};

/* Mode : resolution and related config&values */
struct imx258_mode {
	/* Frame width */
	u32 width;
	/* Frame height */
	u32 height;

	/* V-timing */
	u32 vts_def;
	u32 vts_min;

	/* Index of Link frequency config to be used */
	u32 link_freq_index;
	/* Default register values */
	struct imx258_reg_list reg_list;

	/* Analog crop rectangle */
	struct v4l2_rect crop;
};

/*
 * 4208x3120 @ 30 fps needs 1267Mbps/lane, 4 lanes.
 * To avoid further computation of clock settings, adopt the same per
 * lane data rate when using 2 lanes, thus allowing a maximum of 15fps.
 */
static const struct cci_reg_sequence mipi_1267mbps_19_2mhz_2l[] = {
	{ IMX258_REG_EXCK_FREQ, 0x1333 },
	{ IMX258_REG_IVTPXCK_DIV, 10 },
	{ IMX258_REG_IVTSYCK_DIV, 2 },
	{ IMX258_REG_PREPLLCK_VT_DIV, 3 },
	{ IMX258_REG_PLL_IVT_MPY, 198 },
	{ IMX258_REG_IOPPXCK_DIV, 10 },
	{ IMX258_REG_IOPSYCK_DIV, 1 },
	{ IMX258_REG_PREPLLCK_OP_DIV, 2 },
	{ IMX258_REG_PLL_IOP_MPY, 216 },
	{ IMX258_REG_PLL_MULT_DRIV, 0 },

	{ IMX258_REG_CSI_LANE_MODE, 1 },
	{ IMX258_REG_REQ_LINK_BIT_RATE_MBPS_H, 1267 * 2 },
	{ IMX258_REG_REQ_LINK_BIT_RATE_MBPS_L, 0 },
};

static const struct cci_reg_sequence mipi_1267mbps_19_2mhz_4l[] = {
	{ IMX258_REG_EXCK_FREQ, 0x1333 },
	{ IMX258_REG_IVTPXCK_DIV, 5 },
	{ IMX258_REG_IVTSYCK_DIV, 2 },
	{ IMX258_REG_PREPLLCK_VT_DIV, 3 },
	{ IMX258_REG_PLL_IVT_MPY, 198 },
	{ IMX258_REG_IOPPXCK_DIV, 10 },
	{ IMX258_REG_IOPSYCK_DIV, 1 },
	{ IMX258_REG_PREPLLCK_OP_DIV, 2 },
	{ IMX258_REG_PLL_IOP_MPY, 216 },
	{ IMX258_REG_PLL_MULT_DRIV, 0 },

	{ IMX258_REG_CSI_LANE_MODE, 3 },
	{ IMX258_REG_REQ_LINK_BIT_RATE_MBPS_H, 1267 * 4 },
	{ IMX258_REG_REQ_LINK_BIT_RATE_MBPS_L, 0 },
};

static const struct cci_reg_sequence mipi_1272mbps_24mhz_2l[] = {
	{ IMX258_REG_EXCK_FREQ, 0x1800 },
	{ IMX258_REG_IVTPXCK_DIV, 10 },
	{ IMX258_REG_IVTSYCK_DIV, 2 },
	{ IMX258_REG_PREPLLCK_VT_DIV, 4 },
	{ IMX258_REG_PLL_IVT_MPY, 212 },
	{ IMX258_REG_IOPPXCK_DIV, 10 },
	{ IMX258_REG_IOPSYCK_DIV, 1 },
	{ IMX258_REG_PREPLLCK_OP_DIV, 2 },
	{ IMX258_REG_PLL_IOP_MPY, 216 },
	{ IMX258_REG_PLL_MULT_DRIV, 0 },

	{ IMX258_REG_CSI_LANE_MODE, 1 },
	{ IMX258_REG_REQ_LINK_BIT_RATE_MBPS_H, 1272 * 2 },
	{ IMX258_REG_REQ_LINK_BIT_RATE_MBPS_L, 0 },
};

static const struct cci_reg_sequence mipi_1272mbps_24mhz_4l[] = {
	{ IMX258_REG_EXCK_FREQ, 0x1800 },
	{ IMX258_REG_IVTPXCK_DIV, 5 },
	{ IMX258_REG_IVTSYCK_DIV, 2 },
	{ IMX258_REG_PREPLLCK_VT_DIV, 4 },
	{ IMX258_REG_PLL_IVT_MPY, 212 },
	{ IMX258_REG_IOPPXCK_DIV, 10 },
	{ IMX258_REG_IOPSYCK_DIV, 1 },
	{ IMX258_REG_PREPLLCK_OP_DIV, 2 },
	{ IMX258_REG_PLL_IOP_MPY, 216 },
	{ IMX258_REG_PLL_MULT_DRIV, 0 },

	{ IMX258_REG_CSI_LANE_MODE, 3 },
	{ IMX258_REG_REQ_LINK_BIT_RATE_MBPS_H, 1272 * 4 },
	{ IMX258_REG_REQ_LINK_BIT_RATE_MBPS_L, 0 },
};

static const struct cci_reg_sequence mipi_640mbps_19_2mhz_2l[] = {
	{ IMX258_REG_EXCK_FREQ, 0x1333 },
	{ IMX258_REG_IVTPXCK_DIV, 5 },
	{ IMX258_REG_IVTSYCK_DIV, 2 },
	{ IMX258_REG_PREPLLCK_VT_DIV, 3 },
	{ IMX258_REG_PLL_IVT_MPY, 100 },
	{ IMX258_REG_IOPPXCK_DIV, 10 },
	{ IMX258_REG_IOPSYCK_DIV, 1 },
	{ IMX258_REG_PREPLLCK_OP_DIV, 2 },
	{ IMX258_REG_PLL_IOP_MPY, 216 },
	{ IMX258_REG_PLL_MULT_DRIV, 0 },

	{ IMX258_REG_CSI_LANE_MODE, 1 },
	{ IMX258_REG_REQ_LINK_BIT_RATE_MBPS_H, 640 * 2 },
	{ IMX258_REG_REQ_LINK_BIT_RATE_MBPS_L, 0 },
};

static const struct cci_reg_sequence mipi_640mbps_19_2mhz_4l[] = {
	{ IMX258_REG_EXCK_FREQ, 0x1333 },
	{ IMX258_REG_IVTPXCK_DIV, 5 },
	{ IMX258_REG_IVTSYCK_DIV, 2 },
	{ IMX258_REG_PREPLLCK_VT_DIV, 3 },
	{ IMX258_REG_PLL_IVT_MPY, 100 },
	{ IMX258_REG_IOPPXCK_DIV, 10 },
	{ IMX258_REG_IOPSYCK_DIV, 1 },
	{ IMX258_REG_PREPLLCK_OP_DIV, 2 },
	{ IMX258_REG_PLL_IOP_MPY, 216 },
	{ IMX258_REG_PLL_MULT_DRIV, 0 },

	{ IMX258_REG_CSI_LANE_MODE, 3 },
	{ IMX258_REG_REQ_LINK_BIT_RATE_MBPS_H, 640 * 4 },
	{ IMX258_REG_REQ_LINK_BIT_RATE_MBPS_L, 0 },
};

static const struct cci_reg_sequence mipi_642mbps_24mhz_2l[] = {
	{ IMX258_REG_EXCK_FREQ, 0x1800 },
	{ IMX258_REG_IVTPXCK_DIV, 5 },
	{ IMX258_REG_IVTSYCK_DIV, 2 },
	{ IMX258_REG_PREPLLCK_VT_DIV, 4 },
	{ IMX258_REG_PLL_IVT_MPY, 107 },
	{ IMX258_REG_IOPPXCK_DIV, 10 },
	{ IMX258_REG_IOPSYCK_DIV, 1 },
	{ IMX258_REG_PREPLLCK_OP_DIV, 2 },
	{ IMX258_REG_PLL_IOP_MPY, 216 },
	{ IMX258_REG_PLL_MULT_DRIV, 0 },

	{ IMX258_REG_CSI_LANE_MODE, 1 },
	{ IMX258_REG_REQ_LINK_BIT_RATE_MBPS_H, 642 * 2 },
	{ IMX258_REG_REQ_LINK_BIT_RATE_MBPS_L, 0 },
};

static const struct cci_reg_sequence mipi_642mbps_24mhz_4l[] = {
	{ IMX258_REG_EXCK_FREQ, 0x1800 },
	{ IMX258_REG_IVTPXCK_DIV, 5 },
	{ IMX258_REG_IVTSYCK_DIV, 2 },
	{ IMX258_REG_PREPLLCK_VT_DIV, 4 },
	{ IMX258_REG_PLL_IVT_MPY, 107 },
	{ IMX258_REG_IOPPXCK_DIV, 10 },
	{ IMX258_REG_IOPSYCK_DIV, 1 },
	{ IMX258_REG_PREPLLCK_OP_DIV, 2 },
	{ IMX258_REG_PLL_IOP_MPY, 216 },
	{ IMX258_REG_PLL_MULT_DRIV, 0 },

	{ IMX258_REG_CSI_LANE_MODE, 3 },
	{ IMX258_REG_REQ_LINK_BIT_RATE_MBPS_H, 642 * 4 },
	{ IMX258_REG_REQ_LINK_BIT_RATE_MBPS_L, 0 },
};

static const struct cci_reg_sequence mode_common_regs[] = {
	{ CCI_REG8(0x3051), 0x00 },
	{ CCI_REG8(0x6B11), 0xCF },
	{ CCI_REG8(0x7FF0), 0x08 },
	{ CCI_REG8(0x7FF1), 0x0F },
	{ CCI_REG8(0x7FF2), 0x08 },
	{ CCI_REG8(0x7FF3), 0x1B },
	{ CCI_REG8(0x7FF4), 0x23 },
	{ CCI_REG8(0x7FF5), 0x60 },
	{ CCI_REG8(0x7FF6), 0x00 },
	{ CCI_REG8(0x7FF7), 0x01 },
	{ CCI_REG8(0x7FF8), 0x00 },
	{ CCI_REG8(0x7FF9), 0x78 },
	{ CCI_REG8(0x7FFA), 0x00 },
	{ CCI_REG8(0x7FFB), 0x00 },
	{ CCI_REG8(0x7FFC), 0x00 },
	{ CCI_REG8(0x7FFD), 0x00 },
	{ CCI_REG8(0x7FFE), 0x00 },
	{ CCI_REG8(0x7FFF), 0x03 },
	{ CCI_REG8(0x7F76), 0x03 },
	{ CCI_REG8(0x7F77), 0xFE },
	{ CCI_REG8(0x7FA8), 0x03 },
	{ CCI_REG8(0x7FA9), 0xFE },
	{ CCI_REG8(0x7B24), 0x81 },
	{ CCI_REG8(0x6564), 0x07 },
	{ CCI_REG8(0x6B0D), 0x41 },
	{ CCI_REG8(0x653D), 0x04 },
	{ CCI_REG8(0x6B05), 0x8C },
	{ CCI_REG8(0x6B06), 0xF9 },
	{ CCI_REG8(0x6B08), 0x65 },
	{ CCI_REG8(0x6B09), 0xFC },
	{ CCI_REG8(0x6B0A), 0xCF },
	{ CCI_REG8(0x6B0B), 0xD2 },
	{ CCI_REG8(0x6700), 0x0E },
	{ CCI_REG8(0x6707), 0x0E },
	{ CCI_REG8(0x9104), 0x00 },
	{ CCI_REG8(0x4648), 0x7F },
	{ CCI_REG8(0x7420), 0x00 },
	{ CCI_REG8(0x7421), 0x1C },
	{ CCI_REG8(0x7422), 0x00 },
	{ CCI_REG8(0x7423), 0xD7 },
	{ CCI_REG8(0x5F04), 0x00 },
	{ CCI_REG8(0x5F05), 0xED },
	{IMX258_REG_CSI_DT_FMT, 0x0a0a},
	{IMX258_REG_LINE_LENGTH_PCK, 5352},
	{IMX258_REG_X_ADD_STA, 0},
	{IMX258_REG_Y_ADD_STA, 0},
	{IMX258_REG_X_ADD_END, 4207},
	{IMX258_REG_Y_ADD_END, 3119},
	{IMX258_REG_X_EVN_INC, 1},
	{IMX258_REG_X_ODD_INC, 1},
	{IMX258_REG_Y_EVN_INC, 1},
	{IMX258_REG_Y_ODD_INC, 1},
	{IMX258_REG_DIG_CROP_X_OFFSET, 0},
	{IMX258_REG_DIG_CROP_Y_OFFSET, 0},
	{IMX258_REG_DIG_CROP_IMAGE_WIDTH, 4208},
	{IMX258_REG_SCALE_MODE_EXT, 0},
	{IMX258_REG_SCALE_M_EXT, 16},
	{IMX258_REG_FORCE_FD_SUM, 0},
	{IMX258_REG_FRM_LENGTH_CTL, 0},
	{IMX258_REG_ANALOG_GAIN, 0},
	{IMX258_REG_GR_DIGITAL_GAIN, 256},
	{IMX258_REG_R_DIGITAL_GAIN, 256},
	{IMX258_REG_B_DIGITAL_GAIN, 256},
	{IMX258_REG_GB_DIGITAL_GAIN, 256},
	{IMX258_REG_AF_WINDOW_MODE, 0},
	{ CCI_REG8(0x94DC), 0x20 },
	{ CCI_REG8(0x94DD), 0x20 },
	{ CCI_REG8(0x94DE), 0x20 },
	{ CCI_REG8(0x95DC), 0x20 },
	{ CCI_REG8(0x95DD), 0x20 },
	{ CCI_REG8(0x95DE), 0x20 },
	{ CCI_REG8(0x7FB0), 0x00 },
	{ CCI_REG8(0x9010), 0x3E },
	{ CCI_REG8(0x9419), 0x50 },
	{ CCI_REG8(0x941B), 0x50 },
	{ CCI_REG8(0x9519), 0x50 },
	{ CCI_REG8(0x951B), 0x50 },
	{IMX258_REG_PHASE_PIX_OUTEN, 0},
	{IMX258_REG_PDPIX_DATA_RATE, 0},
	{IMX258_REG_HDR, 0},
};

static const struct cci_reg_sequence mode_4208x3120_regs[] = {
	{IMX258_REG_BINNING_MODE, 0},
	{IMX258_REG_BINNING_TYPE_V, 0x11},
	{IMX258_REG_SCALE_MODE, 0},
	{IMX258_REG_SCALE_M, 16},
	{IMX258_REG_DIG_CROP_IMAGE_HEIGHT, 3120},
	{IMX258_REG_X_OUT_SIZE, 4208},
	{IMX258_REG_Y_OUT_SIZE, 3120},
};

static const struct cci_reg_sequence mode_2104_1560_regs[] = {
	{IMX258_REG_BINNING_MODE, 1},
	{IMX258_REG_BINNING_TYPE_V, 0x12},
	{IMX258_REG_SCALE_MODE, 1},
	{IMX258_REG_SCALE_M, 32},
	{IMX258_REG_DIG_CROP_IMAGE_HEIGHT, 1560},
	{IMX258_REG_X_OUT_SIZE, 2104},
	{IMX258_REG_Y_OUT_SIZE, 1560},
};

static const struct cci_reg_sequence mode_1048_780_regs[] = {
	{IMX258_REG_BINNING_MODE, 1},
	{IMX258_REG_BINNING_TYPE_V, 0x14},
	{IMX258_REG_SCALE_MODE, 1},
	{IMX258_REG_SCALE_M, 64},
	{IMX258_REG_DIG_CROP_IMAGE_HEIGHT, 780},
	{IMX258_REG_X_OUT_SIZE, 1048},
	{IMX258_REG_Y_OUT_SIZE, 780},
};

struct imx258_variant_cfg {
	const struct cci_reg_sequence *regs;
	unsigned int num_regs;
};

static const struct cci_reg_sequence imx258_cfg_regs[] = {
	{ CCI_REG8(0x3052), 0x00 },
	{ CCI_REG8(0x4E21), 0x14 },
	{ CCI_REG8(0x7B25), 0x00 },
};

static const struct imx258_variant_cfg imx258_cfg = {
	.regs = imx258_cfg_regs,
	.num_regs = ARRAY_SIZE(imx258_cfg_regs),
};

static const struct cci_reg_sequence imx258_pdaf_cfg_regs[] = {
	{ CCI_REG8(0x3052), 0x01 },
	{ CCI_REG8(0x4E21), 0x10 },
	{ CCI_REG8(0x7B25), 0x01 },
};

static const struct imx258_variant_cfg imx258_pdaf_cfg = {
	.regs = imx258_pdaf_cfg_regs,
	.num_regs = ARRAY_SIZE(imx258_pdaf_cfg_regs),
};

/*
 * The supported formats.
 * This table MUST contain 4 entries per format, to cover the various flip
 * combinations in the order
 * - no flip
 * - h flip
 * - v flip
 * - h&v flips
 */
static const u32 codes[] = {
	/* 10-bit modes. */
	MEDIA_BUS_FMT_SRGGB10_1X10,
	MEDIA_BUS_FMT_SGRBG10_1X10,
	MEDIA_BUS_FMT_SGBRG10_1X10,
	MEDIA_BUS_FMT_SBGGR10_1X10
};

static const char * const imx258_test_pattern_menu[] = {
	"Disabled",
	"Solid Colour",
	"Eight Vertical Colour Bars",
	"Colour Bars With Fade to Grey",
	"Pseudorandom Sequence (PN9)",
};

/* regulator supplies */
static const char * const imx258_supply_name[] = {
	/* Supplies can be enabled in any order */
	"vana",  /* Analog (2.8V) supply */
	"vdig",  /* Digital Core (1.2V) supply */
	"vif",  /* IF (1.8V) supply */
};

#define IMX258_NUM_SUPPLIES ARRAY_SIZE(imx258_supply_name)

enum {
	IMX258_LINK_FREQ_1267MBPS,
	IMX258_LINK_FREQ_640MBPS,
};

/*
 * Pixel rate does not necessarily relate to link frequency on this sensor as
 * there is a FIFO between the pixel array pipeline and the MIPI serializer.
 * The recommendation from Sony is that the pixel array is always run with a
 * line length of 5352 pixels, which means that there is a large amount of
 * blanking time for the 1048x780 mode. There is no need to replicate this
 * blanking on the CSI2 bus, and the configuration of register 0x0301 allows the
 * divider to be altered.
 *
 * The actual factor between link frequency and pixel rate is in the
 * imx258_link_cfg, so use this to convert between the two.
 * bits per pixel being 10, and D-PHY being DDR is assumed by this function, so
 * the value is only the combination of number of lanes and pixel clock divider.
 */
static u64 link_freq_to_pixel_rate(u64 f, const struct imx258_link_cfg *link_cfg)
{
	f *= 2 * link_cfg->lf_to_pix_rate_factor;
	do_div(f, 10);

	return f;
}

/* Menu items for LINK_FREQ V4L2 control */
/* Configurations for supported link frequencies */
static const s64 link_freq_menu_items_19_2[] = {
	633600000ULL,
	320000000ULL,
};

static const s64 link_freq_menu_items_24[] = {
	636000000ULL,
	321000000ULL,
};

#define REGS(_list) { .num_of_regs = ARRAY_SIZE(_list), .regs = _list, }

/* Link frequency configs */
static const struct imx258_link_freq_config link_freq_configs_19_2[] = {
	[IMX258_LINK_FREQ_1267MBPS] = {
		.pixels_per_line = IMX258_PPL_DEFAULT,
		.link_cfg = {
			[IMX258_2_LANE_MODE] = {
				.lf_to_pix_rate_factor = 2 * 2,
				.reg_list = REGS(mipi_1267mbps_19_2mhz_2l),
			},
			[IMX258_4_LANE_MODE] = {
				.lf_to_pix_rate_factor = 4,
				.reg_list = REGS(mipi_1267mbps_19_2mhz_4l),
			},
		}
	},
	[IMX258_LINK_FREQ_640MBPS] = {
		.pixels_per_line = IMX258_PPL_DEFAULT,
		.link_cfg = {
			[IMX258_2_LANE_MODE] = {
				.lf_to_pix_rate_factor = 2,
				.reg_list = REGS(mipi_640mbps_19_2mhz_2l),
			},
			[IMX258_4_LANE_MODE] = {
				.lf_to_pix_rate_factor = 4,
				.reg_list = REGS(mipi_640mbps_19_2mhz_4l),
			},
		}
	},
};

static const struct imx258_link_freq_config link_freq_configs_24[] = {
	[IMX258_LINK_FREQ_1267MBPS] = {
		.pixels_per_line = IMX258_PPL_DEFAULT,
		.link_cfg = {
			[IMX258_2_LANE_MODE] = {
				.lf_to_pix_rate_factor = 2,
				.reg_list = REGS(mipi_1272mbps_24mhz_2l),
			},
			[IMX258_4_LANE_MODE] = {
				.lf_to_pix_rate_factor = 4,
				.reg_list = REGS(mipi_1272mbps_24mhz_4l),
			},
		}
	},
	[IMX258_LINK_FREQ_640MBPS] = {
		.pixels_per_line = IMX258_PPL_DEFAULT,
		.link_cfg = {
			[IMX258_2_LANE_MODE] = {
				.lf_to_pix_rate_factor = 2 * 2,
				.reg_list = REGS(mipi_642mbps_24mhz_2l),
			},
			[IMX258_4_LANE_MODE] = {
				.lf_to_pix_rate_factor = 4,
				.reg_list = REGS(mipi_642mbps_24mhz_4l),
			},
		}
	},
};

/* Mode configs */
static const struct imx258_mode supported_modes[] = {
	{
		.width = 4208,
		.height = 3120,
		.vts_def = IMX258_VTS_30FPS,
		.vts_min = IMX258_VTS_30FPS,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_4208x3120_regs),
			.regs = mode_4208x3120_regs,
		},
		.link_freq_index = IMX258_LINK_FREQ_1267MBPS,
		.crop = {
			.left = IMX258_PIXEL_ARRAY_LEFT,
			.top = IMX258_PIXEL_ARRAY_TOP,
			.width = 4208,
			.height = 3120,
		},
	},
	{
		.width = 2104,
		.height = 1560,
		.vts_def = IMX258_VTS_30FPS_2K,
		.vts_min = IMX258_VTS_30FPS_2K,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_2104_1560_regs),
			.regs = mode_2104_1560_regs,
		},
		.link_freq_index = IMX258_LINK_FREQ_640MBPS,
		.crop = {
			.left = IMX258_PIXEL_ARRAY_LEFT,
			.top = IMX258_PIXEL_ARRAY_TOP,
			.width = 4208,
			.height = 3120,
		},
	},
	{
		.width = 1048,
		.height = 780,
		.vts_def = IMX258_VTS_30FPS_VGA,
		.vts_min = IMX258_VTS_30FPS_VGA,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1048_780_regs),
			.regs = mode_1048_780_regs,
		},
		.link_freq_index = IMX258_LINK_FREQ_640MBPS,
		.crop = {
			.left = IMX258_PIXEL_ARRAY_LEFT,
			.top = IMX258_PIXEL_ARRAY_TOP,
			.width = 4208,
			.height = 3120,
		},
	},
};

struct imx258 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct regmap *regmap;

	const struct imx258_variant_cfg *variant_cfg;

	struct v4l2_ctrl_handler ctrl_handler;
	/* V4L2 Controls */
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;

	/* Current mode */
	const struct imx258_mode *cur_mode;

	unsigned long link_freq_bitmap;
	const struct imx258_link_freq_config *link_freq_configs;
	const s64 *link_freq_menu_items;
	unsigned int lane_mode_idx;
	unsigned int csi2_flags;

	/*
	 * Mutex for serialized access:
	 * Protect sensor module set pad format and start/stop streaming safely.
	 */
	struct mutex mutex;

	struct clk *clk;
	struct regulator_bulk_data supplies[IMX258_NUM_SUPPLIES];
};

static inline struct imx258 *to_imx258(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct imx258, sd);
}

/* Get bayer order based on flip setting. */
static u32 imx258_get_format_code(const struct imx258 *imx258)
{
	unsigned int i;

	lockdep_assert_held(&imx258->mutex);

	i = (imx258->vflip->val ? 2 : 0) |
	    (imx258->hflip->val ? 1 : 0);

	return codes[i];
}

/* Open sub-device */
static int imx258_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct imx258 *imx258 = to_imx258(sd);
	struct v4l2_mbus_framefmt *try_fmt =
		v4l2_subdev_state_get_format(fh->state, 0);
	struct v4l2_rect *try_crop;

	/* Initialize try_fmt */
	try_fmt->width = supported_modes[0].width;
	try_fmt->height = supported_modes[0].height;
	try_fmt->code = imx258_get_format_code(imx258);
	try_fmt->field = V4L2_FIELD_NONE;

	/* Initialize try_crop */
	try_crop = v4l2_subdev_state_get_crop(fh->state, 0);
	try_crop->left = IMX258_PIXEL_ARRAY_LEFT;
	try_crop->top = IMX258_PIXEL_ARRAY_TOP;
	try_crop->width = IMX258_PIXEL_ARRAY_WIDTH;
	try_crop->height = IMX258_PIXEL_ARRAY_HEIGHT;

	return 0;
}

static int imx258_update_digital_gain(struct imx258 *imx258, u32 val)
{
	int ret = 0;

	cci_write(imx258->regmap, IMX258_REG_GR_DIGITAL_GAIN, val, &ret);
	cci_write(imx258->regmap, IMX258_REG_GB_DIGITAL_GAIN, val, &ret);
	cci_write(imx258->regmap, IMX258_REG_R_DIGITAL_GAIN, val, &ret);
	cci_write(imx258->regmap, IMX258_REG_B_DIGITAL_GAIN, val, &ret);

	return ret;
}

static void imx258_adjust_exposure_range(struct imx258 *imx258)
{
	int exposure_max, exposure_def;

	/* Honour the VBLANK limits when setting exposure. */
	exposure_max = imx258->cur_mode->height + imx258->vblank->val -
		       IMX258_EXPOSURE_OFFSET;
	exposure_def = min(exposure_max, imx258->exposure->val);
	__v4l2_ctrl_modify_range(imx258->exposure, imx258->exposure->minimum,
				 exposure_max, imx258->exposure->step,
				 exposure_def);
}

static int imx258_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx258 *imx258 =
		container_of(ctrl->handler, struct imx258, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&imx258->sd);
	int ret = 0;

	/*
	 * The VBLANK control may change the limits of usable exposure, so check
	 * and adjust if necessary.
	 */
	if (ctrl->id == V4L2_CID_VBLANK)
		imx258_adjust_exposure_range(imx258);

	/*
	 * Applying V4L2 control value only happens
	 * when power is up for streaming
	 */
	if (pm_runtime_get_if_in_use(&client->dev) == 0)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = cci_write(imx258->regmap, IMX258_REG_ANALOG_GAIN,
				ctrl->val, NULL);
		break;
	case V4L2_CID_EXPOSURE:
		ret = cci_write(imx258->regmap, IMX258_REG_EXPOSURE,
				ctrl->val, NULL);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		ret = imx258_update_digital_gain(imx258, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = cci_write(imx258->regmap, IMX258_REG_TEST_PATTERN,
				ctrl->val, NULL);
		break;
	case V4L2_CID_WIDE_DYNAMIC_RANGE:
		if (!ctrl->val) {
			ret = cci_write(imx258->regmap, IMX258_REG_HDR,
					IMX258_HDR_RATIO_MIN, NULL);
		} else {
			ret = cci_write(imx258->regmap, IMX258_REG_HDR,
					IMX258_HDR_ON, NULL);
			if (ret)
				break;
			ret = cci_write(imx258->regmap, IMX258_REG_HDR_RATIO,
					BIT(IMX258_HDR_RATIO_MAX), NULL);
		}
		break;
	case V4L2_CID_VBLANK:
		ret = cci_write(imx258->regmap, IMX258_REG_FRM_LENGTH_LINES,
				imx258->cur_mode->height + ctrl->val, NULL);
		break;
	case V4L2_CID_VFLIP:
	case V4L2_CID_HFLIP:
		ret = cci_write(imx258->regmap, REG_MIRROR_FLIP_CONTROL,
				(imx258->hflip->val ?
				 REG_CONFIG_MIRROR_HFLIP : 0) |
				(imx258->vflip->val ?
				 REG_CONFIG_MIRROR_VFLIP : 0),
				NULL);
		break;
	default:
		dev_info(&client->dev,
			 "ctrl(id:0x%x,val:0x%x) is not handled\n",
			 ctrl->id, ctrl->val);
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx258_ctrl_ops = {
	.s_ctrl = imx258_set_ctrl,
};

static int imx258_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx258 *imx258 = to_imx258(sd);

	/* Only one bayer format (10 bit) is supported */
	if (code->index > 0)
		return -EINVAL;

	code->code = imx258_get_format_code(imx258);

	return 0;
}

static int imx258_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct imx258 *imx258 = to_imx258(sd);
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != imx258_get_format_code(imx258))
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static void imx258_update_pad_format(struct imx258 *imx258,
				     const struct imx258_mode *mode,
				     struct v4l2_subdev_format *fmt)
{
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.code = imx258_get_format_code(imx258);
	fmt->format.field = V4L2_FIELD_NONE;
}

static int __imx258_get_pad_format(struct imx258 *imx258,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_format *fmt)
{
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		fmt->format = *v4l2_subdev_state_get_format(sd_state,
							    fmt->pad);
	else
		imx258_update_pad_format(imx258, imx258->cur_mode, fmt);

	return 0;
}

static int imx258_get_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx258 *imx258 = to_imx258(sd);
	int ret;

	mutex_lock(&imx258->mutex);
	ret = __imx258_get_pad_format(imx258, sd_state, fmt);
	mutex_unlock(&imx258->mutex);

	return ret;
}

static int imx258_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx258 *imx258 = to_imx258(sd);
	const struct imx258_link_freq_config *link_freq_cfgs;
	const struct imx258_link_cfg *link_cfg;
	struct v4l2_mbus_framefmt *framefmt;
	const struct imx258_mode *mode;
	s32 vblank_def;
	s32 vblank_min;
	s64 h_blank;
	s64 pixel_rate;
	s64 link_freq;

	mutex_lock(&imx258->mutex);

	fmt->format.code = imx258_get_format_code(imx258);

	mode = v4l2_find_nearest_size(supported_modes,
		ARRAY_SIZE(supported_modes), width, height,
		fmt->format.width, fmt->format.height);
	imx258_update_pad_format(imx258, mode, fmt);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		framefmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
		*framefmt = fmt->format;
	} else {
		imx258->cur_mode = mode;
		__v4l2_ctrl_s_ctrl(imx258->link_freq, mode->link_freq_index);

		link_freq = imx258->link_freq_menu_items[mode->link_freq_index];
		link_freq_cfgs =
			&imx258->link_freq_configs[mode->link_freq_index];

		link_cfg = &link_freq_cfgs->link_cfg[imx258->lane_mode_idx];
		pixel_rate = link_freq_to_pixel_rate(link_freq, link_cfg);
		__v4l2_ctrl_modify_range(imx258->pixel_rate, pixel_rate,
					 pixel_rate, 1, pixel_rate);
		/* Update limits and set FPS to default */
		vblank_def = imx258->cur_mode->vts_def -
			     imx258->cur_mode->height;
		vblank_min = imx258->cur_mode->vts_min -
			     imx258->cur_mode->height;
		__v4l2_ctrl_modify_range(
			imx258->vblank, vblank_min,
			IMX258_VTS_MAX - imx258->cur_mode->height, 1,
			vblank_def);
		__v4l2_ctrl_s_ctrl(imx258->vblank, vblank_def);
		h_blank =
			imx258->link_freq_configs[mode->link_freq_index].pixels_per_line
			 - imx258->cur_mode->width;
		__v4l2_ctrl_modify_range(imx258->hblank, h_blank,
					 h_blank, 1, h_blank);
	}

	mutex_unlock(&imx258->mutex);

	return 0;
}

static const struct v4l2_rect *
__imx258_get_pad_crop(struct imx258 *imx258,
		      struct v4l2_subdev_state *sd_state,
		      unsigned int pad, enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_state_get_crop(sd_state, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &imx258->cur_mode->crop;
	}

	return NULL;
}

static int imx258_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP: {
		struct imx258 *imx258 = to_imx258(sd);

		mutex_lock(&imx258->mutex);
		sel->r = *__imx258_get_pad_crop(imx258, sd_state, sel->pad,
						sel->which);
		mutex_unlock(&imx258->mutex);

		return 0;
	}

	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = IMX258_NATIVE_WIDTH;
		sel->r.height = IMX258_NATIVE_HEIGHT;

		return 0;

	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = IMX258_PIXEL_ARRAY_LEFT;
		sel->r.top = IMX258_PIXEL_ARRAY_TOP;
		sel->r.width = IMX258_PIXEL_ARRAY_WIDTH;
		sel->r.height = IMX258_PIXEL_ARRAY_HEIGHT;

		return 0;
	}

	return -EINVAL;
}

/* Start streaming */
static int imx258_start_streaming(struct imx258 *imx258)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx258->sd);
	const struct imx258_reg_list *reg_list;
	const struct imx258_link_freq_config *link_freq_cfg;
	int ret, link_freq_index;

	ret = cci_write(imx258->regmap, IMX258_REG_RESET, 0x01, NULL);
	if (ret) {
		dev_err(&client->dev, "%s failed to reset sensor\n", __func__);
		return ret;
	}

	/* 12ms is required from poweron to standby */
	fsleep(12000);

	/* Setup PLL */
	link_freq_index = imx258->cur_mode->link_freq_index;
	link_freq_cfg = &imx258->link_freq_configs[link_freq_index];

	reg_list = &link_freq_cfg->link_cfg[imx258->lane_mode_idx].reg_list;
	ret = cci_multi_reg_write(imx258->regmap, reg_list->regs, reg_list->num_of_regs, NULL);
	if (ret) {
		dev_err(&client->dev, "%s failed to set plls\n", __func__);
		return ret;
	}

	ret = cci_multi_reg_write(imx258->regmap, mode_common_regs,
				  ARRAY_SIZE(mode_common_regs), NULL);
	if (ret) {
		dev_err(&client->dev, "%s failed to set common regs\n", __func__);
		return ret;
	}

	ret = cci_multi_reg_write(imx258->regmap, imx258->variant_cfg->regs,
				  imx258->variant_cfg->num_regs, NULL);
	if (ret) {
		dev_err(&client->dev, "%s failed to set variant config\n",
			__func__);
		return ret;
	}

	ret = cci_write(imx258->regmap, IMX258_CLK_BLANK_STOP,
			!!(imx258->csi2_flags & V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK),
			NULL);
	if (ret) {
		dev_err(&client->dev, "%s failed to set clock lane mode\n", __func__);
		return ret;
	}

	/* Apply default values of current mode */
	reg_list = &imx258->cur_mode->reg_list;
	ret = cci_multi_reg_write(imx258->regmap, reg_list->regs, reg_list->num_of_regs, NULL);
	if (ret) {
		dev_err(&client->dev, "%s failed to set mode\n", __func__);
		return ret;
	}

	/* Apply customized values from user */
	ret =  __v4l2_ctrl_handler_setup(imx258->sd.ctrl_handler);
	if (ret)
		return ret;

	/* set stream on register */
	return cci_write(imx258->regmap, IMX258_REG_MODE_SELECT,
			 IMX258_MODE_STREAMING, NULL);
}

/* Stop streaming */
static int imx258_stop_streaming(struct imx258 *imx258)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx258->sd);
	int ret;

	/* set stream off register */
	ret = cci_write(imx258->regmap, IMX258_REG_MODE_SELECT,
			IMX258_MODE_STANDBY, NULL);
	if (ret)
		dev_err(&client->dev, "%s failed to set stream\n", __func__);

	/*
	 * Return success even if it was an error, as there is nothing the
	 * caller can do about it.
	 */
	return 0;
}

static int imx258_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx258 *imx258 = to_imx258(sd);
	int ret;

	ret = regulator_bulk_enable(IMX258_NUM_SUPPLIES,
				    imx258->supplies);
	if (ret) {
		dev_err(dev, "%s: failed to enable regulators\n",
			__func__);
		return ret;
	}

	ret = clk_prepare_enable(imx258->clk);
	if (ret) {
		dev_err(dev, "failed to enable clock\n");
		regulator_bulk_disable(IMX258_NUM_SUPPLIES, imx258->supplies);
	}

	return ret;
}

static int imx258_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx258 *imx258 = to_imx258(sd);

	clk_disable_unprepare(imx258->clk);
	regulator_bulk_disable(IMX258_NUM_SUPPLIES, imx258->supplies);

	return 0;
}

static int imx258_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx258 *imx258 = to_imx258(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&imx258->mutex);

	if (enable) {
		ret = pm_runtime_resume_and_get(&client->dev);
		if (ret < 0)
			goto err_unlock;

		/*
		 * Apply default & customized values
		 * and then start streaming.
		 */
		ret = imx258_start_streaming(imx258);
		if (ret)
			goto err_rpm_put;
	} else {
		imx258_stop_streaming(imx258);
		pm_runtime_put(&client->dev);
	}

	mutex_unlock(&imx258->mutex);

	return ret;

err_rpm_put:
	pm_runtime_put(&client->dev);
err_unlock:
	mutex_unlock(&imx258->mutex);

	return ret;
}

/* Verify chip ID */
static int imx258_identify_module(struct imx258 *imx258)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx258->sd);
	int ret;
	u64 val;

	ret = cci_read(imx258->regmap, IMX258_REG_CHIP_ID,
		       &val, NULL);
	if (ret) {
		dev_err(&client->dev, "failed to read chip id %x\n",
			IMX258_CHIP_ID);
		return ret;
	}

	if (val != IMX258_CHIP_ID) {
		dev_err(&client->dev, "chip id mismatch: %x!=%llx\n",
			IMX258_CHIP_ID, val);
		return -EIO;
	}

	return 0;
}

static const struct v4l2_subdev_video_ops imx258_video_ops = {
	.s_stream = imx258_set_stream,
};

static const struct v4l2_subdev_pad_ops imx258_pad_ops = {
	.enum_mbus_code = imx258_enum_mbus_code,
	.get_fmt = imx258_get_pad_format,
	.set_fmt = imx258_set_pad_format,
	.enum_frame_size = imx258_enum_frame_size,
	.get_selection = imx258_get_selection,
};

static const struct v4l2_subdev_ops imx258_subdev_ops = {
	.video = &imx258_video_ops,
	.pad = &imx258_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx258_internal_ops = {
	.open = imx258_open,
};

/* Initialize control handlers */
static int imx258_init_controls(struct imx258 *imx258)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx258->sd);
	const struct imx258_link_freq_config *link_freq_cfgs;
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl_handler *ctrl_hdlr;
	const struct imx258_link_cfg *link_cfg;
	s64 vblank_def;
	s64 vblank_min;
	s64 pixel_rate;
	int ret;

	ctrl_hdlr = &imx258->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 13);
	if (ret)
		return ret;

	mutex_init(&imx258->mutex);
	ctrl_hdlr->lock = &imx258->mutex;
	imx258->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr,
				&imx258_ctrl_ops,
				V4L2_CID_LINK_FREQ,
				ARRAY_SIZE(link_freq_menu_items_19_2) - 1,
				0,
				imx258->link_freq_menu_items);

	if (imx258->link_freq)
		imx258->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx258->hflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx258_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 1);
	if (imx258->hflip)
		imx258->hflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	imx258->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx258_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 1);
	if (imx258->vflip)
		imx258->vflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	link_freq_cfgs = &imx258->link_freq_configs[0];
	link_cfg = link_freq_cfgs[imx258->lane_mode_idx].link_cfg;
	pixel_rate = link_freq_to_pixel_rate(imx258->link_freq_menu_items[0],
					     link_cfg);

	/* By default, PIXEL_RATE is read only */
	imx258->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &imx258_ctrl_ops,
				V4L2_CID_PIXEL_RATE,
				pixel_rate, pixel_rate,
				1, pixel_rate);

	vblank_def = imx258->cur_mode->vts_def - imx258->cur_mode->height;
	vblank_min = imx258->cur_mode->vts_min - imx258->cur_mode->height;
	imx258->vblank = v4l2_ctrl_new_std(
				ctrl_hdlr, &imx258_ctrl_ops, V4L2_CID_VBLANK,
				vblank_min,
				IMX258_VTS_MAX - imx258->cur_mode->height, 1,
				vblank_def);

	imx258->hblank = v4l2_ctrl_new_std(
				ctrl_hdlr, &imx258_ctrl_ops, V4L2_CID_HBLANK,
				IMX258_PPL_DEFAULT - imx258->cur_mode->width,
				IMX258_PPL_DEFAULT - imx258->cur_mode->width,
				1,
				IMX258_PPL_DEFAULT - imx258->cur_mode->width);

	if (imx258->hblank)
		imx258->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx258->exposure = v4l2_ctrl_new_std(
				ctrl_hdlr, &imx258_ctrl_ops,
				V4L2_CID_EXPOSURE, IMX258_EXPOSURE_MIN,
				IMX258_EXPOSURE_MAX, IMX258_EXPOSURE_STEP,
				IMX258_EXPOSURE_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx258_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
				IMX258_ANA_GAIN_MIN, IMX258_ANA_GAIN_MAX,
				IMX258_ANA_GAIN_STEP, IMX258_ANA_GAIN_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx258_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
				IMX258_DGTL_GAIN_MIN, IMX258_DGTL_GAIN_MAX,
				IMX258_DGTL_GAIN_STEP,
				IMX258_DGTL_GAIN_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx258_ctrl_ops, V4L2_CID_WIDE_DYNAMIC_RANGE,
				0, 1, 1, IMX258_HDR_RATIO_DEFAULT);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &imx258_ctrl_ops,
				V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(imx258_test_pattern_menu) - 1,
				0, 0, imx258_test_pattern_menu);

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(&client->dev, "%s control init failed (%d)\n",
				__func__, ret);
		goto error;
	}

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		goto error;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &imx258_ctrl_ops,
					      &props);
	if (ret)
		goto error;

	imx258->sd.ctrl_handler = ctrl_hdlr;

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);
	mutex_destroy(&imx258->mutex);

	return ret;
}

static void imx258_free_controls(struct imx258 *imx258)
{
	v4l2_ctrl_handler_free(imx258->sd.ctrl_handler);
	mutex_destroy(&imx258->mutex);
}

static int imx258_get_regulators(struct imx258 *imx258,
				 struct i2c_client *client)
{
	unsigned int i;

	for (i = 0; i < IMX258_NUM_SUPPLIES; i++)
		imx258->supplies[i].supply = imx258_supply_name[i];

	return devm_regulator_bulk_get(&client->dev,
				    IMX258_NUM_SUPPLIES, imx258->supplies);
}

static int imx258_probe(struct i2c_client *client)
{
	struct imx258 *imx258;
	struct fwnode_handle *endpoint;
	struct v4l2_fwnode_endpoint ep = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	int ret;
	u32 val = 0;

	imx258 = devm_kzalloc(&client->dev, sizeof(*imx258), GFP_KERNEL);
	if (!imx258)
		return -ENOMEM;

	imx258->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(imx258->regmap)) {
		ret = PTR_ERR(imx258->regmap);
		dev_err(&client->dev, "failed to initialize CCI: %d\n", ret);
		return ret;
	}

	ret = imx258_get_regulators(imx258, client);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "failed to get regulators\n");

	imx258->clk = devm_clk_get_optional(&client->dev, NULL);
	if (IS_ERR(imx258->clk))
		return dev_err_probe(&client->dev, PTR_ERR(imx258->clk),
				     "error getting clock\n");
	if (!imx258->clk) {
		dev_dbg(&client->dev,
			"no clock provided, using clock-frequency property\n");

		device_property_read_u32(&client->dev, "clock-frequency", &val);
	} else {
		val = clk_get_rate(imx258->clk);
	}

	switch (val) {
	case 19200000:
		imx258->link_freq_configs = link_freq_configs_19_2;
		imx258->link_freq_menu_items = link_freq_menu_items_19_2;
		break;
	case 24000000:
		imx258->link_freq_configs = link_freq_configs_24;
		imx258->link_freq_menu_items = link_freq_menu_items_24;
		break;
	default:
		dev_err(&client->dev, "input clock frequency of %u not supported\n",
			val);
		return -EINVAL;
	}

	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(&client->dev), NULL);
	if (!endpoint) {
		dev_err(&client->dev, "Endpoint node not found\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_alloc_parse(endpoint, &ep);
	fwnode_handle_put(endpoint);
	if (ret) {
		dev_err(&client->dev, "Parsing endpoint node failed\n");
		return ret;
	}

	ret = v4l2_link_freq_to_bitmap(&client->dev,
				       ep.link_frequencies,
				       ep.nr_of_link_frequencies,
				       imx258->link_freq_menu_items,
				       ARRAY_SIZE(link_freq_menu_items_19_2),
				       &imx258->link_freq_bitmap);
	if (ret) {
		dev_err(&client->dev, "Link frequency not supported\n");
		goto error_endpoint_free;
	}

	/* Get number of data lanes */
	switch (ep.bus.mipi_csi2.num_data_lanes) {
	case 2:
		imx258->lane_mode_idx = IMX258_2_LANE_MODE;
		break;
	case 4:
		imx258->lane_mode_idx = IMX258_4_LANE_MODE;
		break;
	default:
		dev_err(&client->dev, "Invalid data lanes: %u\n",
			ep.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto error_endpoint_free;
	}

	imx258->csi2_flags = ep.bus.mipi_csi2.flags;

	imx258->variant_cfg = device_get_match_data(&client->dev);
	if (!imx258->variant_cfg)
		imx258->variant_cfg = &imx258_cfg;

	/* Initialize subdev */
	v4l2_i2c_subdev_init(&imx258->sd, client, &imx258_subdev_ops);

	/* Will be powered off via pm_runtime_idle */
	ret = imx258_power_on(&client->dev);
	if (ret)
		goto error_endpoint_free;

	/* Check module identity */
	ret = imx258_identify_module(imx258);
	if (ret)
		goto error_identify;

	/* Set default mode to max resolution */
	imx258->cur_mode = &supported_modes[0];

	ret = imx258_init_controls(imx258);
	if (ret)
		goto error_identify;

	/* Initialize subdev */
	imx258->sd.internal_ops = &imx258_internal_ops;
	imx258->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	imx258->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pad */
	imx258->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&imx258->sd.entity, 1, &imx258->pad);
	if (ret)
		goto error_handler_free;

	ret = v4l2_async_register_subdev_sensor(&imx258->sd);
	if (ret < 0)
		goto error_media_entity;

	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);
	v4l2_fwnode_endpoint_free(&ep);

	return 0;

error_media_entity:
	media_entity_cleanup(&imx258->sd.entity);

error_handler_free:
	imx258_free_controls(imx258);

error_identify:
	imx258_power_off(&client->dev);

error_endpoint_free:
	v4l2_fwnode_endpoint_free(&ep);

	return ret;
}

static void imx258_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx258 *imx258 = to_imx258(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	imx258_free_controls(imx258);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		imx258_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);
}

static const struct dev_pm_ops imx258_pm_ops = {
	SET_RUNTIME_PM_OPS(imx258_power_off, imx258_power_on, NULL)
};

#ifdef CONFIG_ACPI
static const struct acpi_device_id imx258_acpi_ids[] = {
	{ "SONY258A" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(acpi, imx258_acpi_ids);
#endif

static const struct of_device_id imx258_dt_ids[] = {
	{ .compatible = "sony,imx258", .data = &imx258_cfg },
	{ .compatible = "sony,imx258-pdaf", .data = &imx258_pdaf_cfg },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx258_dt_ids);

static struct i2c_driver imx258_i2c_driver = {
	.driver = {
		.name = "imx258",
		.pm = &imx258_pm_ops,
		.acpi_match_table = ACPI_PTR(imx258_acpi_ids),
		.of_match_table	= imx258_dt_ids,
	},
	.probe = imx258_probe,
	.remove = imx258_remove,
};

module_i2c_driver(imx258_i2c_driver);

MODULE_AUTHOR("Yeh, Andy <andy.yeh@intel.com>");
MODULE_AUTHOR("Chiang, Alan");
MODULE_AUTHOR("Chen, Jason");
MODULE_DESCRIPTION("Sony IMX258 sensor driver");
MODULE_LICENSE("GPL v2");

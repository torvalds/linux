/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019-2020 NXP
 */

#ifndef __IMX8_ISI_REGS_H__
#define __IMX8_ISI_REGS_H__

#include <linux/bits.h>

/* ISI Registers Define  */
/* Channel Control Register */
#define CHNL_CTRL						0x0000
#define CHNL_CTRL_CHNL_EN					BIT(31)
#define CHNL_CTRL_CLK_EN					BIT(30)
#define CHNL_CTRL_CHNL_BYPASS					BIT(29)
#define CHNL_CTRL_CHAIN_BUF(n)					((n) << 25)
#define CHNL_CTRL_CHAIN_BUF_MASK				GENMASK(26, 25)
#define CHNL_CTRL_CHAIN_BUF_NO_CHAIN				0
#define CHNL_CTRL_CHAIN_BUF_2_CHAIN				1
#define CHNL_CTRL_SW_RST					BIT(24)
#define CHNL_CTRL_BLANK_PXL(n)					((n) << 16)
#define CHNL_CTRL_BLANK_PXL_MASK				GENMASK(23, 16)
#define CHNL_CTRL_MIPI_VC_ID(n)					((n) << 6)
#define CHNL_CTRL_MIPI_VC_ID_MASK				GENMASK(7, 6)
#define CHNL_CTRL_SRC_TYPE(n)					((n) << 4)
#define CHNL_CTRL_SRC_TYPE_MASK					BIT(4)
#define CHNL_CTRL_SRC_TYPE_DEVICE				0
#define CHNL_CTRL_SRC_TYPE_MEMORY				1
#define CHNL_CTRL_SRC_INPUT(n)					((n) << 0)
#define CHNL_CTRL_SRC_INPUT_MASK				GENMASK(2, 0)

/* Channel Image Control Register */
#define CHNL_IMG_CTRL						0x0004
#define CHNL_IMG_CTRL_FORMAT(n)					((n) << 24)
#define CHNL_IMG_CTRL_FORMAT_MASK				GENMASK(29, 24)
#define CHNL_IMG_CTRL_FORMAT_RGBA8888				0x00
#define CHNL_IMG_CTRL_FORMAT_ABGR8888				0x01
#define CHNL_IMG_CTRL_FORMAT_ARGB8888				0x02
#define CHNL_IMG_CTRL_FORMAT_RGBX888				0x03
#define CHNL_IMG_CTRL_FORMAT_XBGR888				0x04
#define CHNL_IMG_CTRL_FORMAT_XRGB888				0x05
#define CHNL_IMG_CTRL_FORMAT_RGB888P				0x06
#define CHNL_IMG_CTRL_FORMAT_BGR888P				0x07
#define CHNL_IMG_CTRL_FORMAT_A2BGR10				0x08
#define CHNL_IMG_CTRL_FORMAT_A2RGB10				0x09
#define CHNL_IMG_CTRL_FORMAT_RGB565				0x0a
#define CHNL_IMG_CTRL_FORMAT_RAW8				0x0b
#define CHNL_IMG_CTRL_FORMAT_RAW10				0x0c
#define CHNL_IMG_CTRL_FORMAT_RAW10P				0x0d
#define CHNL_IMG_CTRL_FORMAT_RAW12				0x0e
#define CHNL_IMG_CTRL_FORMAT_RAW16				0x0f
#define CHNL_IMG_CTRL_FORMAT_YUV444_1P8P			0x10
#define CHNL_IMG_CTRL_FORMAT_YUV444_2P8P			0x11
#define CHNL_IMG_CTRL_FORMAT_YUV444_3P8P			0x12
#define CHNL_IMG_CTRL_FORMAT_YUV444_1P8				0x13
#define CHNL_IMG_CTRL_FORMAT_YUV444_1P10			0x14
#define CHNL_IMG_CTRL_FORMAT_YUV444_2P10			0x15
#define CHNL_IMG_CTRL_FORMAT_YUV444_3P10			0x16
#define CHNL_IMG_CTRL_FORMAT_YUV444_1P10P			0x18
#define CHNL_IMG_CTRL_FORMAT_YUV444_2P10P			0x19
#define CHNL_IMG_CTRL_FORMAT_YUV444_3P10P			0x1a
#define CHNL_IMG_CTRL_FORMAT_YUV444_1P12			0x1c
#define CHNL_IMG_CTRL_FORMAT_YUV444_2P12			0x1d
#define CHNL_IMG_CTRL_FORMAT_YUV444_3P12			0x1e
#define CHNL_IMG_CTRL_FORMAT_YUV422_1P8P			0x20
#define CHNL_IMG_CTRL_FORMAT_YUV422_2P8P			0x21
#define CHNL_IMG_CTRL_FORMAT_YUV422_3P8P			0x22
#define CHNL_IMG_CTRL_FORMAT_YUV422_1P10			0x24
#define CHNL_IMG_CTRL_FORMAT_YUV422_2P10			0x25
#define CHNL_IMG_CTRL_FORMAT_YUV422_3P10			0x26
#define CHNL_IMG_CTRL_FORMAT_YUV422_1P10P			0x28
#define CHNL_IMG_CTRL_FORMAT_YUV422_2P10P			0x29
#define CHNL_IMG_CTRL_FORMAT_YUV422_3P10P			0x2a
#define CHNL_IMG_CTRL_FORMAT_YUV422_1P12			0x2c
#define CHNL_IMG_CTRL_FORMAT_YUV422_2P12			0x2d
#define CHNL_IMG_CTRL_FORMAT_YUV422_3P12			0x2e
#define CHNL_IMG_CTRL_FORMAT_YUV420_2P8P			0x31
#define CHNL_IMG_CTRL_FORMAT_YUV420_3P8P			0x32
#define CHNL_IMG_CTRL_FORMAT_YUV420_2P10			0x35
#define CHNL_IMG_CTRL_FORMAT_YUV420_3P10			0x36
#define CHNL_IMG_CTRL_FORMAT_YUV420_2P10P			0x39
#define CHNL_IMG_CTRL_FORMAT_YUV420_3P10P			0x3a
#define CHNL_IMG_CTRL_FORMAT_YUV420_2P12			0x3d
#define CHNL_IMG_CTRL_FORMAT_YUV420_3P12			0x3e
#define CHNL_IMG_CTRL_GBL_ALPHA_VAL(n)				((n) << 16)
#define CHNL_IMG_CTRL_GBL_ALPHA_VAL_MASK			GENMASK(23, 16)
#define CHNL_IMG_CTRL_GBL_ALPHA_EN				BIT(15)
#define CHNL_IMG_CTRL_DEINT(n)					((n) << 12)
#define CHNL_IMG_CTRL_DEINT_MASK				GENMASK(14, 12)
#define CHNL_IMG_CTRL_DEINT_WEAVE_ODD_EVEN			2
#define CHNL_IMG_CTRL_DEINT_WEAVE_EVEN_ODD			3
#define CHNL_IMG_CTRL_DEINT_BLEND_ODD_EVEN			4
#define CHNL_IMG_CTRL_DEINT_BLEND_EVEN_ODD			5
#define CHNL_IMG_CTRL_DEINT_LDOUBLE_ODD_EVEN			6
#define CHNL_IMG_CTRL_DEINT_LDOUBLE_EVEN_ODD			7
#define CHNL_IMG_CTRL_DEC_X(n)					((n) << 10)
#define CHNL_IMG_CTRL_DEC_X_MASK				GENMASK(11, 10)
#define CHNL_IMG_CTRL_DEC_Y(n)					((n) << 8)
#define CHNL_IMG_CTRL_DEC_Y_MASK				GENMASK(9, 8)
#define CHNL_IMG_CTRL_CROP_EN					BIT(7)
#define CHNL_IMG_CTRL_VFLIP_EN					BIT(6)
#define CHNL_IMG_CTRL_HFLIP_EN					BIT(5)
#define CHNL_IMG_CTRL_YCBCR_MODE				BIT(3)
#define CHNL_IMG_CTRL_CSC_MODE(n)				((n) << 1)
#define CHNL_IMG_CTRL_CSC_MODE_MASK				GENMASK(2, 1)
#define CHNL_IMG_CTRL_CSC_MODE_YUV2RGB				0
#define CHNL_IMG_CTRL_CSC_MODE_YCBCR2RGB			1
#define CHNL_IMG_CTRL_CSC_MODE_RGB2YUV				2
#define CHNL_IMG_CTRL_CSC_MODE_RGB2YCBCR			3
#define CHNL_IMG_CTRL_CSC_BYPASS				BIT(0)

/* Channel Output Buffer Control Register */
#define CHNL_OUT_BUF_CTRL					0x0008
#define CHNL_OUT_BUF_CTRL_LOAD_BUF2_ADDR			BIT(15)
#define CHNL_OUT_BUF_CTRL_LOAD_BUF1_ADDR			BIT(14)
#define CHNL_OUT_BUF_CTRL_OFLW_PANIC_SET_THD_V(n)		((n) << 6)
#define CHNL_OUT_BUF_CTRL_OFLW_PANIC_SET_THD_V_MASK		GENMASK(7, 6)
#define CHNL_OUT_BUF_CTRL_OFLW_PANIC_SET_THD_V_NO_PANIC		0
#define CHNL_OUT_BUF_CTRL_OFLW_PANIC_SET_THD_V_PANIC_25		1
#define CHNL_OUT_BUF_CTRL_OFLW_PANIC_SET_THD_V_PANIC_50		2
#define CHNL_OUT_BUF_CTRL_OFLW_PANIC_SET_THD_V_PANIC_75		3
#define CHNL_OUT_BUF_CTRL_OFLW_PANIC_SET_THD_U(n)		((n) << 3)
#define CHNL_OUT_BUF_CTRL_OFLW_PANIC_SET_THD_U_MASK		GENMASK(4, 3)
#define CHNL_OUT_BUF_CTRL_OFLW_PANIC_SET_THD_U_NO_PANIC		0
#define CHNL_OUT_BUF_CTRL_OFLW_PANIC_SET_THD_U_PANIC_25		1
#define CHNL_OUT_BUF_CTRL_OFLW_PANIC_SET_THD_U_PANIC_50		2
#define CHNL_OUT_BUF_CTRL_OFLW_PANIC_SET_THD_U_PANIC_75		3
#define CHNL_OUT_BUF_CTRL_OFLW_PANIC_SET_THD_Y(n)		((n) << 0)
#define CHNL_OUT_BUF_CTRL_OFLW_PANIC_SET_THD_Y_MASK		GENMASK(1, 0)
#define CHNL_OUT_BUF_CTRL_OFLW_PANIC_SET_THD_Y_NO_PANIC		0
#define CHNL_OUT_BUF_CTRL_OFLW_PANIC_SET_THD_Y_PANIC_25		1
#define CHNL_OUT_BUF_CTRL_OFLW_PANIC_SET_THD_Y_PANIC_50		2
#define CHNL_OUT_BUF_CTRL_OFLW_PANIC_SET_THD_Y_PANIC_75		3

/* Channel Image Configuration */
#define CHNL_IMG_CFG						0x000c
#define CHNL_IMG_CFG_HEIGHT(n)					((n) << 16)
#define CHNL_IMG_CFG_HEIGHT_MASK				GENMASK(28, 16)
#define CHNL_IMG_CFG_WIDTH(n)					((n) << 0)
#define CHNL_IMG_CFG_WIDTH_MASK					GENMASK(12, 0)

/* Channel Interrupt Enable Register */
#define CHNL_IER						0x0010
#define CHNL_IER_MEM_RD_DONE_EN					BIT(31)
#define CHNL_IER_LINE_RCVD_EN					BIT(30)
#define CHNL_IER_FRM_RCVD_EN					BIT(29)
#define CHNL_IER_AXI_WR_ERR_V_EN				BIT(28)
#define CHNL_IER_AXI_WR_ERR_U_EN				BIT(27)
#define CHNL_IER_AXI_WR_ERR_Y_EN				BIT(26)
#define CHNL_IER_AXI_RD_ERR_EN					BIT(25)

/* Channel Status Register */
#define CHNL_STS						0x0014
#define CHNL_STS_MEM_RD_DONE					BIT(31)
#define CHNL_STS_LINE_STRD					BIT(30)
#define CHNL_STS_FRM_STRD					BIT(29)
#define CHNL_STS_AXI_WR_ERR_V					BIT(28)
#define CHNL_STS_AXI_WR_ERR_U					BIT(27)
#define CHNL_STS_AXI_WR_ERR_Y					BIT(26)
#define CHNL_STS_AXI_RD_ERR					BIT(25)
#define CHNL_STS_OFLW_PANIC_V_BUF				BIT(24)
#define CHNL_STS_EXCS_OFLW_V_BUF				BIT(23)
#define CHNL_STS_OFLW_V_BUF					BIT(22)
#define CHNL_STS_OFLW_PANIC_U_BUF				BIT(21)
#define CHNL_STS_EXCS_OFLW_U_BUF				BIT(20)
#define CHNL_STS_OFLW_U_BUF					BIT(19)
#define CHNL_STS_OFLW_PANIC_Y_BUF				BIT(18)
#define CHNL_STS_EXCS_OFLW_Y_BUF				BIT(17)
#define CHNL_STS_OFLW_Y_BUF					BIT(16)
#define CHNL_STS_EARLY_VSYNC_ERR				BIT(15)
#define CHNL_STS_LATE_VSYNC_ERR					BIT(14)
#define CHNL_STS_MEM_RD_OFLOW					BIT(10)
#define CHNL_STS_BUF2_ACTIVE					BIT(9)
#define CHNL_STS_BUF1_ACTIVE					BIT(8)
#define CHNL_STS_OFLW_BYTES(n)					((n) << 0)
#define CHNL_STS_OFLW_BYTES_MASK				GENMASK(7, 0)

/* Channel Scale Factor Register */
#define CHNL_SCALE_FACTOR					0x0018
#define CHNL_SCALE_FACTOR_Y_SCALE(n)				((n) << 16)
#define CHNL_SCALE_FACTOR_Y_SCALE_MASK				GENMASK(29, 16)
#define CHNL_SCALE_FACTOR_X_SCALE(n)				((n) << 0)
#define CHNL_SCALE_FACTOR_X_SCALE_MASK				GENMASK(13, 0)

/* Channel Scale Offset Register */
#define CHNL_SCALE_OFFSET					0x001c
#define CHNL_SCALE_OFFSET_Y_SCALE(n)				((n) << 16)
#define CHNL_SCALE_OFFSET_Y_SCALE_MASK				GENMASK(27, 16)
#define CHNL_SCALE_OFFSET_X_SCALE(n)				((n) << 0)
#define CHNL_SCALE_OFFSET_X_SCALE_MASK				GENMASK(11, 0)

/* Channel Crop Upper Left Corner Coordinate Register */
#define CHNL_CROP_ULC						0x0020
#define CHNL_CROP_ULC_X(n)					((n) << 16)
#define CHNL_CROP_ULC_X_MASK					GENMASK(27, 16)
#define CHNL_CROP_ULC_Y(n)					((n) << 0)
#define CHNL_CROP_ULC_Y_MASK					GENMASK(11, 0)

/* Channel Crop Lower Right Corner Coordinate Register */
#define CHNL_CROP_LRC						0x0024
#define CHNL_CROP_LRC_X(n)					((n) << 16)
#define CHNL_CROP_LRC_X_MASK					GENMASK(27, 16)
#define CHNL_CROP_LRC_Y(n)					((n) << 0)
#define CHNL_CROP_LRC_Y_MASK					GENMASK(11, 0)

/* Channel Color Space Conversion Coefficient Register 0 */
#define CHNL_CSC_COEFF0						0x0028
#define CHNL_CSC_COEFF0_A2(n)					((n) << 16)
#define CHNL_CSC_COEFF0_A2_MASK					GENMASK(26, 16)
#define CHNL_CSC_COEFF0_A1(n)					((n) << 0)
#define CHNL_CSC_COEFF0_A1_MASK					GENMASK(10, 0)

/* Channel Color Space Conversion Coefficient Register 1 */
#define CHNL_CSC_COEFF1						0x002c
#define CHNL_CSC_COEFF1_B1(n)					((n) << 16)
#define CHNL_CSC_COEFF1_B1_MASK					GENMASK(26, 16)
#define CHNL_CSC_COEFF1_A3(n)					((n) << 0)
#define CHNL_CSC_COEFF1_A3_MASK					GENMASK(10, 0)

/* Channel Color Space Conversion Coefficient Register 2 */
#define CHNL_CSC_COEFF2						0x0030
#define CHNL_CSC_COEFF2_B3(n)					((n) << 16)
#define CHNL_CSC_COEFF2_B3_MASK					GENMASK(26, 16)
#define CHNL_CSC_COEFF2_B2(n)					((n) << 0)
#define CHNL_CSC_COEFF2_B2_MASK					GENMASK(10, 0)

/* Channel Color Space Conversion Coefficient Register 3 */
#define CHNL_CSC_COEFF3						0x0034
#define CHNL_CSC_COEFF3_C2(n)					((n) << 16)
#define CHNL_CSC_COEFF3_C2_MASK					GENMASK(26, 16)
#define CHNL_CSC_COEFF3_C1(n)					((n) << 0)
#define CHNL_CSC_COEFF3_C1_MASK					GENMASK(10, 0)

/* Channel Color Space Conversion Coefficient Register 4 */
#define CHNL_CSC_COEFF4						0x0038
#define CHNL_CSC_COEFF4_D1(n)					((n) << 16)
#define CHNL_CSC_COEFF4_D1_MASK					GENMASK(24, 16)
#define CHNL_CSC_COEFF4_C3(n)					((n) << 0)
#define CHNL_CSC_COEFF4_C3_MASK					GENMASK(10, 0)

/* Channel Color Space Conversion Coefficient Register 5 */
#define CHNL_CSC_COEFF5						0x003c
#define CHNL_CSC_COEFF5_D3(n)					((n) << 16)
#define CHNL_CSC_COEFF5_D3_MASK					GENMASK(24, 16)
#define CHNL_CSC_COEFF5_D2(n)					((n) << 0)
#define CHNL_CSC_COEFF5_D2_MASK					GENMASK(8, 0)

/* Channel Alpha Value Register for ROI 0 */
#define CHNL_ROI_0_ALPHA					0x0040
#define CHNL_ROI_0_ALPHA_VAL(n)					((n) << 24)
#define CHNL_ROI_0_ALPHA_MASK					GENMASK(31, 24)
#define CHNL_ROI_0_ALPHA_EN					BIT(16)

/* Channel Upper Left Coordinate Register for ROI 0 */
#define CHNL_ROI_0_ULC						0x0044
#define CHNL_ROI_0_ULC_X(n)					((n) << 16)
#define CHNL_ROI_0_ULC_X_MASK					GENMASK(27, 16)
#define CHNL_ROI_0_ULC_Y(n)					((n) << 0)
#define CHNL_ROI_0_ULC_Y_MASK					GENMASK(11, 0)

/* Channel Lower Right Coordinate Register for ROI 0 */
#define CHNL_ROI_0_LRC						0x0048
#define CHNL_ROI_0_LRC_X(n)					((n) << 16)
#define CHNL_ROI_0_LRC_X_MASK					GENMASK(27, 16)
#define CHNL_ROI_0_LRC_Y(n)					((n) << 0)
#define CHNL_ROI_0_LRC_Y_MASK					GENMASK(11, 0)

/* Channel Alpha Value Register for ROI 1 */
#define CHNL_ROI_1_ALPHA					0x004c
#define CHNL_ROI_1_ALPHA_VAL(n)					((n) << 24)
#define CHNL_ROI_1_ALPHA_MASK					GENMASK(31, 24)
#define CHNL_ROI_1_ALPHA_EN					BIT(16)

/* Channel Upper Left Coordinate Register for ROI 1 */
#define CHNL_ROI_1_ULC						0x0050
#define CHNL_ROI_1_ULC_X(n)					((n) << 16)
#define CHNL_ROI_1_ULC_X_MASK					GENMASK(27, 16)
#define CHNL_ROI_1_ULC_Y(n)					((n) << 0)
#define CHNL_ROI_1_ULC_Y_MASK					GENMASK(11, 0)

/* Channel Lower Right Coordinate Register for ROI 1 */
#define CHNL_ROI_1_LRC						0x0054
#define CHNL_ROI_1_LRC_X(n)					((n) << 16)
#define CHNL_ROI_1_LRC_X_MASK					GENMASK(27, 16)
#define CHNL_ROI_1_LRC_Y(n)					((n) << 0)
#define CHNL_ROI_1_LRC_Y_MASK					GENMASK(11, 0)

/* Channel Alpha Value Register for ROI 2 */
#define CHNL_ROI_2_ALPHA					0x0058
#define CHNL_ROI_2_ALPHA_VAL(n)					((n) << 24)
#define CHNL_ROI_2_ALPHA_MASK					GENMASK(31, 24)
#define CHNL_ROI_2_ALPHA_EN					BIT(16)

/* Channel Upper Left Coordinate Register for ROI 2 */
#define CHNL_ROI_2_ULC						0x005c
#define CHNL_ROI_2_ULC_X(n)					((n) << 16)
#define CHNL_ROI_2_ULC_X_MASK					GENMASK(27, 16)
#define CHNL_ROI_2_ULC_Y(n)					((n) << 0)
#define CHNL_ROI_2_ULC_Y_MASK					GENMASK(11, 0)

/* Channel Lower Right Coordinate Register for ROI 2 */
#define CHNL_ROI_2_LRC						0x0060
#define CHNL_ROI_2_LRC_X(n)					((n) << 16)
#define CHNL_ROI_2_LRC_X_MASK					GENMASK(27, 16)
#define CHNL_ROI_2_LRC_Y(n)					((n) << 0)
#define CHNL_ROI_2_LRC_Y_MASK					GENMASK(11, 0)

/* Channel Alpha Value Register for ROI 3 */
#define CHNL_ROI_3_ALPHA					0x0064
#define CHNL_ROI_3_ALPHA_VAL(n)					((n) << 24)
#define CHNL_ROI_3_ALPHA_MASK					GENMASK(31, 24)
#define CHNL_ROI_3_ALPHA_EN					BIT(16)

/* Channel Upper Left Coordinate Register for ROI 3 */
#define CHNL_ROI_3_ULC						0x0068
#define CHNL_ROI_3_ULC_X(n)					((n) << 16)
#define CHNL_ROI_3_ULC_X_MASK					GENMASK(27, 16)
#define CHNL_ROI_3_ULC_Y(n)					((n) << 0)
#define CHNL_ROI_3_ULC_Y_MASK					GENMASK(11, 0)

/* Channel Lower Right Coordinate Register for ROI 3 */
#define CHNL_ROI_3_LRC						0x006c
#define CHNL_ROI_3_LRC_X(n)					((n) << 16)
#define CHNL_ROI_3_LRC_X_MASK					GENMASK(27, 16)
#define CHNL_ROI_3_LRC_Y(n)					((n) << 0)
#define CHNL_ROI_3_LRC_Y_MASK					GENMASK(11, 0)
/* Channel RGB or Luma (Y) Output Buffer 1 Address */
#define CHNL_OUT_BUF1_ADDR_Y					0x0070

/* Channel Chroma (U/Cb/UV/CbCr) Output Buffer 1 Address */
#define CHNL_OUT_BUF1_ADDR_U					0x0074

/* Channel Chroma (V/Cr) Output Buffer 1 Address */
#define CHNL_OUT_BUF1_ADDR_V					0x0078

/* Channel Output Buffer Pitch */
#define CHNL_OUT_BUF_PITCH					0x007c
#define CHNL_OUT_BUF_PITCH_LINE_PITCH(n)			((n) << 0)
#define CHNL_OUT_BUF_PITCH_LINE_PITCH_MASK			GENMASK(15, 0)

/* Channel Input Buffer Address */
#define CHNL_IN_BUF_ADDR					0x0080

/* Channel Input Buffer Pitch */
#define CHNL_IN_BUF_PITCH					0x0084
#define CHNL_IN_BUF_PITCH_FRM_PITCH(n)				((n) << 16)
#define CHNL_IN_BUF_PITCH_FRM_PITCH_MASK			GENMASK(31, 16)
#define CHNL_IN_BUF_PITCH_LINE_PITCH(n)				((n) << 0)
#define CHNL_IN_BUF_PITCH_LINE_PITCH_MASK			GENMASK(15, 0)

/* Channel Memory Read Control */
#define CHNL_MEM_RD_CTRL					0x0088
#define CHNL_MEM_RD_CTRL_IMG_TYPE(n)				((n) << 28)
#define CHNL_MEM_RD_CTRL_IMG_TYPE_MASK				GENMASK(31, 28)
#define CHNL_MEM_RD_CTRL_IMG_TYPE_BGR8P				0x00
#define CHNL_MEM_RD_CTRL_IMG_TYPE_RGB8P				0x01
#define CHNL_MEM_RD_CTRL_IMG_TYPE_XRGB8				0x02
#define CHNL_MEM_RD_CTRL_IMG_TYPE_RGBX8				0x03
#define CHNL_MEM_RD_CTRL_IMG_TYPE_XBGR8				0x04
#define CHNL_MEM_RD_CTRL_IMG_TYPE_RGB565			0x05
#define CHNL_MEM_RD_CTRL_IMG_TYPE_A2BGR10			0x06
#define CHNL_MEM_RD_CTRL_IMG_TYPE_A2RGB10			0x07
#define CHNL_MEM_RD_CTRL_IMG_TYPE_YUV444_1P8P			0x08
#define CHNL_MEM_RD_CTRL_IMG_TYPE_YUV444_1P10			0x09
#define CHNL_MEM_RD_CTRL_IMG_TYPE_YUV444_1P10P			0x0a
#define CHNL_MEM_RD_CTRL_IMG_TYPE_YUV444_1P12			0x0b
#define CHNL_MEM_RD_CTRL_IMG_TYPE_YUV444_1P8			0x0c
#define CHNL_MEM_RD_CTRL_IMG_TYPE_YUV422_1P8P			0x0d
#define CHNL_MEM_RD_CTRL_IMG_TYPE_YUV422_1P10			0x0e
#define CHNL_MEM_RD_CTRL_IMG_TYPE_YUV422_1P12			0x0f
#define CHNL_MEM_RD_CTRL_READ_MEM				BIT(0)

/* Channel RGB or Luma (Y) Output Buffer 2 Address */
#define CHNL_OUT_BUF2_ADDR_Y					0x008c

/* Channel Chroma (U/Cb/UV/CbCr) Output Buffer 2 Address  */
#define CHNL_OUT_BUF2_ADDR_U					0x0090

/* Channel Chroma (V/Cr) Output Buffer 2 Address   */
#define CHNL_OUT_BUF2_ADDR_V					0x0094

/* Channel scale image config */
#define CHNL_SCL_IMG_CFG					0x0098
#define CHNL_SCL_IMG_CFG_HEIGHT(n)				((n) << 16)
#define CHNL_SCL_IMG_CFG_HEIGHT_MASK				GENMASK(28, 16)
#define CHNL_SCL_IMG_CFG_WIDTH(n)				((n) << 0)
#define CHNL_SCL_IMG_CFG_WIDTH_MASK				GENMASK(12, 0)

/* Channel Flow Control Register */
#define CHNL_FLOW_CTRL						0x009c
#define CHNL_FLOW_CTRL_FC_DENOM_MASK				GENMASK(7, 0)
#define CHNL_FLOW_CTRL_FC_DENOM(n)				((n) << 0)
#define CHNL_FLOW_CTRL_FC_NUMER_MASK				GENMASK(23, 16)
#define CHNL_FLOW_CTRL_FC_NUMER(n)				((n) << 0)

/* Channel Output Y-Buffer 1 Extended Address Bits */
#define CHNL_Y_BUF1_XTND_ADDR					0x00a0

/* Channel Output U-Buffer 1 Extended Address Bits */
#define CHNL_U_BUF1_XTND_ADDR					0x00a4

/* Channel Output V-Buffer 1 Extended Address Bits */
#define CHNL_V_BUF1_XTND_ADDR					0x00a8

/* Channel Output Y-Buffer 2 Extended Address Bits */
#define CHNL_Y_BUF2_XTND_ADDR					0x00ac

/* Channel Output U-Buffer 2 Extended Address Bits */
#define CHNL_U_BUF2_XTND_ADDR					0x00b0

/* Channel Output V-Buffer 2 Extended Address Bits */
#define CHNL_V_BUF2_XTND_ADDR					0x00b4

/* Channel Input Buffer Extended Address Bits */
#define CHNL_IN_BUF_XTND_ADDR					0x00b8

#endif /* __IMX8_ISI_REGS_H__ */

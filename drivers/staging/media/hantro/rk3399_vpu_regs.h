/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Hantro VPU codec driver
 *
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 *	Alpha Lin <alpha.lin@rock-chips.com>
 */

#ifndef RK3399_VPU_REGS_H_
#define RK3399_VPU_REGS_H_

/* Encoder registers. */
#define VEPU_REG_VP8_QUT_1ST(i)			(0x000 + ((i) * 0x24))
#define     VEPU_REG_VP8_QUT_DC_Y2(x)			(((x) & 0x3fff) << 16)
#define     VEPU_REG_VP8_QUT_DC_Y1(x)			(((x) & 0x3fff) << 0)
#define VEPU_REG_VP8_QUT_2ND(i)			(0x004 + ((i) * 0x24))
#define     VEPU_REG_VP8_QUT_AC_Y1(x)			(((x) & 0x3fff) << 16)
#define     VEPU_REG_VP8_QUT_DC_CHR(x)			(((x) & 0x3fff) << 0)
#define VEPU_REG_VP8_QUT_3RD(i)			(0x008 + ((i) * 0x24))
#define     VEPU_REG_VP8_QUT_AC_CHR(x)			(((x) & 0x3fff) << 16)
#define     VEPU_REG_VP8_QUT_AC_Y2(x)			(((x) & 0x3fff) << 0)
#define VEPU_REG_VP8_QUT_4TH(i)			(0x00c + ((i) * 0x24))
#define     VEPU_REG_VP8_QUT_ZB_DC_CHR(x)		(((x) & 0x1ff) << 18)
#define     VEPU_REG_VP8_QUT_ZB_DC_Y2(x)		(((x) & 0x1ff) << 9)
#define     VEPU_REG_VP8_QUT_ZB_DC_Y1(x)		(((x) & 0x1ff) << 0)
#define VEPU_REG_VP8_QUT_5TH(i)			(0x010 + ((i) * 0x24))
#define     VEPU_REG_VP8_QUT_ZB_AC_CHR(x)		(((x) & 0x1ff) << 18)
#define     VEPU_REG_VP8_QUT_ZB_AC_Y2(x)		(((x) & 0x1ff) << 9)
#define     VEPU_REG_VP8_QUT_ZB_AC_Y1(x)		(((x) & 0x1ff) << 0)
#define VEPU_REG_VP8_QUT_6TH(i)			(0x014 + ((i) * 0x24))
#define     VEPU_REG_VP8_QUT_RND_DC_CHR(x)		(((x) & 0xff) << 16)
#define     VEPU_REG_VP8_QUT_RND_DC_Y2(x)		(((x) & 0xff) << 8)
#define     VEPU_REG_VP8_QUT_RND_DC_Y1(x)		(((x) & 0xff) << 0)
#define VEPU_REG_VP8_QUT_7TH(i)			(0x018 + ((i) * 0x24))
#define     VEPU_REG_VP8_QUT_RND_AC_CHR(x)		(((x) & 0xff) << 16)
#define     VEPU_REG_VP8_QUT_RND_AC_Y2(x)		(((x) & 0xff) << 8)
#define     VEPU_REG_VP8_QUT_RND_AC_Y1(x)		(((x) & 0xff) << 0)
#define VEPU_REG_VP8_QUT_8TH(i)			(0x01c + ((i) * 0x24))
#define     VEPU_REG_VP8_SEG_FILTER_LEVEL(x)		(((x) & 0x3f) << 25)
#define     VEPU_REG_VP8_DEQUT_DC_CHR(x)		(((x) & 0xff) << 17)
#define     VEPU_REG_VP8_DEQUT_DC_Y2(x)			(((x) & 0x1ff) << 8)
#define     VEPU_REG_VP8_DEQUT_DC_Y1(x)			(((x) & 0xff) << 0)
#define VEPU_REG_VP8_QUT_9TH(i)			(0x020 + ((i) * 0x24))
#define     VEPU_REG_VP8_DEQUT_AC_CHR(x)		(((x) & 0x1ff) << 18)
#define     VEPU_REG_VP8_DEQUT_AC_Y2(x)			(((x) & 0x1ff) << 9)
#define     VEPU_REG_VP8_DEQUT_AC_Y1(x)			(((x) & 0x1ff) << 0)
#define VEPU_REG_ADDR_VP8_SEG_MAP		0x06c
#define VEPU_REG_VP8_INTRA_4X4_PENALTY(i)	(0x070 + ((i) * 0x4))
#define     VEPU_REG_VP8_INTRA_4X4_PENALTY_0(x)		(((x) & 0xfff) << 0)
#define     VEPU_REG_VP8_INTRA_4x4_PENALTY_1(x)		(((x) & 0xfff) << 16)
#define VEPU_REG_VP8_INTRA_16X16_PENALTY(i)	(0x084 + ((i) * 0x4))
#define     VEPU_REG_VP8_INTRA_16X16_PENALTY_0(x)	(((x) & 0xfff) << 0)
#define     VEPU_REG_VP8_INTRA_16X16_PENALTY_1(x)	(((x) & 0xfff) << 16)
#define VEPU_REG_VP8_CONTROL			0x0a0
#define     VEPU_REG_VP8_LF_MODE_DELTA_BPRED(x)		(((x) & 0x1f) << 24)
#define     VEPU_REG_VP8_LF_REF_DELTA_INTRA_MB(x)	(((x) & 0x7f) << 16)
#define     VEPU_REG_VP8_INTER_TYPE_BIT_COST(x)		(((x) & 0xfff) << 0)
#define VEPU_REG_VP8_REF_FRAME_VAL		0x0a4
#define     VEPU_REG_VP8_COEF_DMV_PENALTY(x)		(((x) & 0xfff) << 16)
#define     VEPU_REG_VP8_REF_FRAME(x)			(((x) & 0xfff) << 0)
#define VEPU_REG_VP8_LOOP_FILTER_REF_DELTA	0x0a8
#define     VEPU_REG_VP8_LF_REF_DELTA_ALT_REF(x)	(((x) & 0x7f) << 16)
#define     VEPU_REG_VP8_LF_REF_DELTA_LAST_REF(x)	(((x) & 0x7f) << 8)
#define     VEPU_REG_VP8_LF_REF_DELTA_GOLDEN(x)		(((x) & 0x7f) << 0)
#define VEPU_REG_VP8_LOOP_FILTER_MODE_DELTA	0x0ac
#define     VEPU_REG_VP8_LF_MODE_DELTA_SPLITMV(x)	(((x) & 0x7f) << 16)
#define     VEPU_REG_VP8_LF_MODE_DELTA_ZEROMV(x)	(((x) & 0x7f) << 8)
#define     VEPU_REG_VP8_LF_MODE_DELTA_NEWMV(x)		(((x) & 0x7f) << 0)
#define	VEPU_REG_JPEG_LUMA_QUAT(i)		(0x000 + ((i) * 0x4))
#define	VEPU_REG_JPEG_CHROMA_QUAT(i)		(0x040 + ((i) * 0x4))
#define VEPU_REG_INTRA_SLICE_BITMAP(i)		(0x0b0 + ((i) * 0x4))
#define VEPU_REG_ADDR_VP8_DCT_PART(i)		(0x0b0 + ((i) * 0x4))
#define VEPU_REG_INTRA_AREA_CTRL		0x0b8
#define     VEPU_REG_INTRA_AREA_TOP(x)			(((x) & 0xff) << 24)
#define     VEPU_REG_INTRA_AREA_BOTTOM(x)		(((x) & 0xff) << 16)
#define     VEPU_REG_INTRA_AREA_LEFT(x)			(((x) & 0xff) << 8)
#define     VEPU_REG_INTRA_AREA_RIGHT(x)		(((x) & 0xff) << 0)
#define VEPU_REG_CIR_INTRA_CTRL			0x0bc
#define     VEPU_REG_CIR_INTRA_FIRST_MB(x)		(((x) & 0xffff) << 16)
#define     VEPU_REG_CIR_INTRA_INTERVAL(x)		(((x) & 0xffff) << 0)
#define VEPU_REG_ADDR_IN_PLANE_0		0x0c0
#define VEPU_REG_ADDR_IN_PLANE_1		0x0c4
#define VEPU_REG_ADDR_IN_PLANE_2		0x0c8
#define VEPU_REG_STR_HDR_REM_MSB		0x0cc
#define VEPU_REG_STR_HDR_REM_LSB		0x0d0
#define VEPU_REG_STR_BUF_LIMIT			0x0d4
#define VEPU_REG_AXI_CTRL			0x0d8
#define     VEPU_REG_AXI_CTRL_READ_ID(x)		(((x) & 0xff) << 24)
#define     VEPU_REG_AXI_CTRL_WRITE_ID(x)		(((x) & 0xff) << 16)
#define     VEPU_REG_AXI_CTRL_BURST_LEN(x)		(((x) & 0x3f) << 8)
#define     VEPU_REG_AXI_CTRL_INCREMENT_MODE(x)		(((x) & 0x01) << 2)
#define     VEPU_REG_AXI_CTRL_BIRST_DISCARD(x)		(((x) & 0x01) << 1)
#define     VEPU_REG_AXI_CTRL_BIRST_DISABLE		BIT(0)
#define VEPU_QP_ADJUST_MAD_DELTA_ROI		0x0dc
#define     VEPU_REG_ROI_QP_DELTA_1			(((x) & 0xf) << 12)
#define     VEPU_REG_ROI_QP_DELTA_2			(((x) & 0xf) << 8)
#define     VEPU_REG_MAD_QP_ADJUSTMENT			(((x) & 0xf) << 0)
#define VEPU_REG_ADDR_REF_LUMA			0x0e0
#define VEPU_REG_ADDR_REF_CHROMA		0x0e4
#define VEPU_REG_QP_SUM_DIV2			0x0e8
#define     VEPU_REG_QP_SUM(x)				(((x) & 0x001fffff) * 2)
#define VEPU_REG_ENC_CTRL0			0x0ec
#define     VEPU_REG_DISABLE_QUARTER_PIXEL_MV		BIT(28)
#define     VEPU_REG_DEBLOCKING_FILTER_MODE(x)		(((x) & 0x3) << 24)
#define     VEPU_REG_CABAC_INIT_IDC(x)			(((x) & 0x3) << 21)
#define     VEPU_REG_ENTROPY_CODING_MODE		BIT(20)
#define     VEPU_REG_H264_TRANS8X8_MODE			BIT(17)
#define     VEPU_REG_H264_INTER4X4_MODE			BIT(16)
#define     VEPU_REG_H264_STREAM_MODE			BIT(15)
#define     VEPU_REG_H264_SLICE_SIZE(x)			(((x) & 0x7f) << 8)
#define VEPU_REG_ENC_OVER_FILL_STRM_OFFSET	0x0f0
#define     VEPU_REG_STREAM_START_OFFSET(x)		(((x) & 0x3f) << 16)
#define     VEPU_REG_SKIP_MACROBLOCK_PENALTY(x)		(((x) & 0xff) << 8)
#define     VEPU_REG_IN_IMG_CTRL_OVRFLR_D4(x)		(((x) & 0x3) << 4)
#define     VEPU_REG_IN_IMG_CTRL_OVRFLB(x)		(((x) & 0xf) << 0)
#define VEPU_REG_INPUT_LUMA_INFO		0x0f4
#define     VEPU_REG_IN_IMG_CHROMA_OFFSET(x)		(((x) & 0x7) << 20)
#define     VEPU_REG_IN_IMG_LUMA_OFFSET(x)		(((x) & 0x7) << 16)
#define     VEPU_REG_IN_IMG_CTRL_ROW_LEN(x)		(((x) & 0x3fff) << 0)
#define VEPU_REG_RLC_SUM			0x0f8
#define     VEPU_REG_RLC_SUM_OUT(x)			(((x) & 0x007fffff) * 4)
#define VEPU_REG_SPLIT_PENALTY_4X4		0x0f8
#define	    VEPU_REG_VP8_SPLIT_PENALTY_4X4		(((x) & 0x1ff) << 19)
#define VEPU_REG_ADDR_REC_LUMA			0x0fc
#define VEPU_REG_ADDR_REC_CHROMA		0x100
#define VEPU_REG_CHECKPOINT(i)			(0x104 + ((i) * 0x4))
#define     VEPU_REG_CHECKPOINT_CHECK0(x)		(((x) & 0xffff))
#define     VEPU_REG_CHECKPOINT_CHECK1(x)		(((x) & 0xffff) << 16)
#define     VEPU_REG_CHECKPOINT_RESULT(x) \
		((((x) >> (16 - 16 * ((i) & 1))) & 0xffff) * 32)
#define VEPU_REG_VP8_SEG0_QUANT_AC_Y1		0x104
#define     VEPU_REG_VP8_SEG0_RND_AC_Y1(x)		(((x) & 0xff) << 23)
#define     VEPU_REG_VP8_SEG0_ZBIN_AC_Y1(x)		(((x) & 0x1ff) << 14)
#define     VEPU_REG_VP8_SEG0_QUT_AC_Y1(x)		(((x) & 0x3fff) << 0)
#define VEPU_REG_VP8_SEG0_QUANT_DC_Y2		0x108
#define     VEPU_REG_VP8_SEG0_RND_DC_Y2(x)		(((x) & 0xff) << 23)
#define     VEPU_REG_VP8_SEG0_ZBIN_DC_Y2(x)		(((x) & 0x1ff) << 14)
#define     VEPU_REG_VP8_SEG0_QUT_DC_Y2(x)		(((x) & 0x3fff) << 0)
#define VEPU_REG_VP8_SEG0_QUANT_AC_Y2		0x10c
#define     VEPU_REG_VP8_SEG0_RND_AC_Y2(x)		(((x) & 0xff) << 23)
#define     VEPU_REG_VP8_SEG0_ZBIN_AC_Y2(x)		(((x) & 0x1ff) << 14)
#define     VEPU_REG_VP8_SEG0_QUT_AC_Y2(x)		(((x) & 0x3fff) << 0)
#define VEPU_REG_VP8_SEG0_QUANT_DC_CHR		0x110
#define     VEPU_REG_VP8_SEG0_RND_DC_CHR(x)		(((x) & 0xff) << 23)
#define     VEPU_REG_VP8_SEG0_ZBIN_DC_CHR(x)		(((x) & 0x1ff) << 14)
#define     VEPU_REG_VP8_SEG0_QUT_DC_CHR(x)		(((x) & 0x3fff) << 0)
#define VEPU_REG_VP8_SEG0_QUANT_AC_CHR		0x114
#define     VEPU_REG_VP8_SEG0_RND_AC_CHR(x)		(((x) & 0xff) << 23)
#define     VEPU_REG_VP8_SEG0_ZBIN_AC_CHR(x)		(((x) & 0x1ff) << 14)
#define     VEPU_REG_VP8_SEG0_QUT_AC_CHR(x)		(((x) & 0x3fff) << 0)
#define VEPU_REG_VP8_SEG0_QUANT_DQUT		0x118
#define     VEPU_REG_VP8_MV_REF_IDX1(x)			(((x) & 0x03) << 26)
#define     VEPU_REG_VP8_SEG0_DQUT_DC_Y2(x)		(((x) & 0x1ff) << 17)
#define     VEPU_REG_VP8_SEG0_DQUT_AC_Y1(x)		(((x) & 0x1ff) << 8)
#define     VEPU_REG_VP8_SEG0_DQUT_DC_Y1(x)		(((x) & 0xff) << 0)
#define VEPU_REG_CHKPT_WORD_ERR(i)		(0x118 + ((i) * 0x4))
#define     VEPU_REG_CHKPT_WORD_ERR_CHK0(x)		(((x) & 0xffff))
#define     VEPU_REG_CHKPT_WORD_ERR_CHK1(x)		(((x) & 0xffff) << 16)
#define VEPU_REG_VP8_SEG0_QUANT_DQUT_1		0x11c
#define     VEPU_REG_VP8_SEGMENT_MAP_UPDATE		BIT(30)
#define     VEPU_REG_VP8_SEGMENT_EN			BIT(29)
#define     VEPU_REG_VP8_MV_REF_IDX2_EN			BIT(28)
#define     VEPU_REG_VP8_MV_REF_IDX2(x)			(((x) & 0x03) << 26)
#define     VEPU_REG_VP8_SEG0_DQUT_AC_CHR(x)		(((x) & 0x1ff) << 17)
#define     VEPU_REG_VP8_SEG0_DQUT_DC_CHR(x)		(((x) & 0xff) << 9)
#define     VEPU_REG_VP8_SEG0_DQUT_AC_Y2(x)		(((x) & 0x1ff) << 0)
#define VEPU_REG_VP8_BOOL_ENC_VALUE		0x120
#define VEPU_REG_CHKPT_DELTA_QP			0x124
#define     VEPU_REG_CHKPT_DELTA_QP_CHK0(x)		(((x) & 0x0f) << 0)
#define     VEPU_REG_CHKPT_DELTA_QP_CHK1(x)		(((x) & 0x0f) << 4)
#define     VEPU_REG_CHKPT_DELTA_QP_CHK2(x)		(((x) & 0x0f) << 8)
#define     VEPU_REG_CHKPT_DELTA_QP_CHK3(x)		(((x) & 0x0f) << 12)
#define     VEPU_REG_CHKPT_DELTA_QP_CHK4(x)		(((x) & 0x0f) << 16)
#define     VEPU_REG_CHKPT_DELTA_QP_CHK5(x)		(((x) & 0x0f) << 20)
#define     VEPU_REG_CHKPT_DELTA_QP_CHK6(x)		(((x) & 0x0f) << 24)
#define VEPU_REG_VP8_ENC_CTRL2			0x124
#define     VEPU_REG_VP8_ZERO_MV_PENALTY_FOR_REF2(x)	(((x) & 0xff) << 24)
#define     VEPU_REG_VP8_FILTER_SHARPNESS(x)		(((x) & 0x07) << 21)
#define     VEPU_REG_VP8_FILTER_LEVEL(x)		(((x) & 0x3f) << 15)
#define     VEPU_REG_VP8_DCT_PARTITION_CNT(x)		(((x) & 0x03) << 13)
#define     VEPU_REG_VP8_BOOL_ENC_VALUE_BITS(x)		(((x) & 0x1f) << 8)
#define     VEPU_REG_VP8_BOOL_ENC_RANGE(x)		(((x) & 0xff) << 0)
#define VEPU_REG_ENC_CTRL1			0x128
#define     VEPU_REG_MAD_THRESHOLD(x)			(((x) & 0x3f) << 24)
#define     VEPU_REG_COMPLETED_SLICES(x)		(((x) & 0xff) << 16)
#define     VEPU_REG_IN_IMG_CTRL_FMT(x)			(((x) & 0xf) << 4)
#define     VEPU_REG_IN_IMG_ROTATE_MODE(x)		(((x) & 0x3) << 2)
#define     VEPU_REG_SIZE_TABLE_PRESENT			BIT(0)
#define VEPU_REG_INTRA_INTER_MODE		0x12c
#define     VEPU_REG_INTRA16X16_MODE(x)			(((x) & 0xffff) << 16)
#define     VEPU_REG_INTER_MODE(x)			(((x) & 0xffff) << 0)
#define VEPU_REG_ENC_CTRL2			0x130
#define     VEPU_REG_PPS_INIT_QP(x)			(((x) & 0x3f) << 26)
#define     VEPU_REG_SLICE_FILTER_ALPHA(x)		(((x) & 0xf) << 22)
#define     VEPU_REG_SLICE_FILTER_BETA(x)		(((x) & 0xf) << 18)
#define     VEPU_REG_CHROMA_QP_OFFSET(x)		(((x) & 0x1f) << 13)
#define     VEPU_REG_FILTER_DISABLE			BIT(5)
#define     VEPU_REG_IDR_PIC_ID(x)			(((x) & 0xf) << 1)
#define     VEPU_REG_CONSTRAINED_INTRA_PREDICTION	BIT(0)
#define VEPU_REG_ADDR_OUTPUT_STREAM		0x134
#define VEPU_REG_ADDR_OUTPUT_CTRL		0x138
#define VEPU_REG_ADDR_NEXT_PIC			0x13c
#define VEPU_REG_ADDR_MV_OUT			0x140
#define VEPU_REG_ADDR_CABAC_TBL			0x144
#define VEPU_REG_ROI1				0x148
#define     VEPU_REG_ROI1_TOP_MB(x)			(((x) & 0xff) << 24)
#define     VEPU_REG_ROI1_BOTTOM_MB(x)			(((x) & 0xff) << 16)
#define     VEPU_REG_ROI1_LEFT_MB(x)			(((x) & 0xff) << 8)
#define     VEPU_REG_ROI1_RIGHT_MB(x)			(((x) & 0xff) << 0)
#define VEPU_REG_ROI2				0x14c
#define     VEPU_REG_ROI2_TOP_MB(x)			(((x) & 0xff) << 24)
#define     VEPU_REG_ROI2_BOTTOM_MB(x)			(((x) & 0xff) << 16)
#define     VEPU_REG_ROI2_LEFT_MB(x)			(((x) & 0xff) << 8)
#define     VEPU_REG_ROI2_RIGHT_MB(x)			(((x) & 0xff) << 0)
#define VEPU_REG_STABLE_MATRIX(i)		(0x150 + ((i) * 0x4))
#define VEPU_REG_STABLE_MOTION_SUM		0x174
#define VEPU_REG_STABILIZATION_OUTPUT		0x178
#define     VEPU_REG_STABLE_MIN_VALUE(x)		(((x) & 0xffffff) << 8)
#define     VEPU_REG_STABLE_MODE_SEL(x)			(((x) & 0x3) << 6)
#define     VEPU_REG_STABLE_HOR_GMV(x)			(((x) & 0x3f) << 0)
#define VEPU_REG_RGB2YUV_CONVERSION_COEF1	0x17c
#define     VEPU_REG_RGB2YUV_CONVERSION_COEFB(x)	(((x) & 0xffff) << 16)
#define     VEPU_REG_RGB2YUV_CONVERSION_COEFA(x)	(((x) & 0xffff) << 0)
#define VEPU_REG_RGB2YUV_CONVERSION_COEF2	0x180
#define     VEPU_REG_RGB2YUV_CONVERSION_COEFE(x)	(((x) & 0xffff) << 16)
#define     VEPU_REG_RGB2YUV_CONVERSION_COEFC(x)	(((x) & 0xffff) << 0)
#define VEPU_REG_RGB2YUV_CONVERSION_COEF3	0x184
#define     VEPU_REG_RGB2YUV_CONVERSION_COEFF(x)	(((x) & 0xffff) << 0)
#define VEPU_REG_RGB_MASK_MSB			0x188
#define     VEPU_REG_RGB_MASK_B_MSB(x)			(((x) & 0x1f) << 16)
#define     VEPU_REG_RGB_MASK_G_MSB(x)			(((x) & 0x1f) << 8)
#define     VEPU_REG_RGB_MASK_R_MSB(x)			(((x) & 0x1f) << 0)
#define VEPU_REG_MV_PENALTY			0x18c
#define     VEPU_REG_1MV_PENALTY(x)			(((x) & 0x3ff) << 21)
#define     VEPU_REG_QMV_PENALTY(x)			(((x) & 0x3ff) << 11)
#define     VEPU_REG_4MV_PENALTY(x)			(((x) & 0x3ff) << 1)
#define     VEPU_REG_SPLIT_MV_MODE_EN			BIT(0)
#define VEPU_REG_QP_VAL				0x190
#define     VEPU_REG_H264_LUMA_INIT_QP(x)		(((x) & 0x3f) << 26)
#define     VEPU_REG_H264_QP_MAX(x)			(((x) & 0x3f) << 20)
#define     VEPU_REG_H264_QP_MIN(x)			(((x) & 0x3f) << 14)
#define     VEPU_REG_H264_CHKPT_DISTANCE(x)		(((x) & 0xfff) << 0)
#define VEPU_REG_VP8_SEG0_QUANT_DC_Y1		0x190
#define     VEPU_REG_VP8_SEG0_RND_DC_Y1(x)		(((x) & 0xff) << 23)
#define     VEPU_REG_VP8_SEG0_ZBIN_DC_Y1(x)		(((x) & 0x1ff) << 14)
#define     VEPU_REG_VP8_SEG0_QUT_DC_Y1(x)		(((x) & 0x3fff) << 0)
#define VEPU_REG_MVC_RELATE			0x198
#define     VEPU_REG_ZERO_MV_FAVOR_D2(x)		(((x) & 0xf) << 20)
#define     VEPU_REG_PENALTY_4X4MV(x)			(((x) & 0x1ff) << 11)
#define     VEPU_REG_MVC_VIEW_ID(x)			(((x) & 0x7) << 8)
#define     VEPU_REG_MVC_ANCHOR_PIC_FLAG		BIT(7)
#define     VEPU_REG_MVC_PRIORITY_ID(x)			(((x) & 0x7) << 4)
#define     VEPU_REG_MVC_TEMPORAL_ID(x)			(((x) & 0x7) << 1)
#define     VEPU_REG_MVC_INTER_VIEW_FLAG		BIT(0)
#define VEPU_REG_ENCODE_START			0x19c
#define     VEPU_REG_MB_HEIGHT(x)			(((x) & 0x1ff) << 20)
#define     VEPU_REG_MB_WIDTH(x)			(((x) & 0x1ff) << 8)
#define     VEPU_REG_FRAME_TYPE_INTER			(0x0 << 6)
#define     VEPU_REG_FRAME_TYPE_INTRA			(0x1 << 6)
#define     VEPU_REG_FRAME_TYPE_MVCINTER		(0x2 << 6)
#define     VEPU_REG_ENCODE_FORMAT_JPEG			(0x2 << 4)
#define     VEPU_REG_ENCODE_FORMAT_H264			(0x3 << 4)
#define     VEPU_REG_ENCODE_ENABLE			BIT(0)
#define VEPU_REG_MB_CTRL			0x1a0
#define     VEPU_REG_MB_CNT_OUT(x)			(((x) & 0xffff) << 16)
#define     VEPU_REG_MB_CNT_SET(x)			(((x) & 0xffff) << 0)
#define VEPU_REG_DATA_ENDIAN			0x1a4
#define     VEPU_REG_INPUT_SWAP8			BIT(31)
#define     VEPU_REG_INPUT_SWAP16			BIT(30)
#define     VEPU_REG_INPUT_SWAP32			BIT(29)
#define     VEPU_REG_OUTPUT_SWAP8			BIT(28)
#define     VEPU_REG_OUTPUT_SWAP16			BIT(27)
#define     VEPU_REG_OUTPUT_SWAP32			BIT(26)
#define     VEPU_REG_TEST_IRQ				BIT(24)
#define     VEPU_REG_TEST_COUNTER(x)			(((x) & 0xf) << 20)
#define     VEPU_REG_TEST_REG				BIT(19)
#define     VEPU_REG_TEST_MEMORY			BIT(18)
#define     VEPU_REG_TEST_LEN(x)			(((x) & 0x3ffff) << 0)
#define VEPU_REG_ENC_CTRL3			0x1a8
#define     VEPU_REG_PPS_ID(x)				(((x) & 0xff) << 24)
#define     VEPU_REG_INTRA_PRED_MODE(x)			(((x) & 0xff) << 16)
#define     VEPU_REG_FRAME_NUM(x)			(((x) & 0xffff) << 0)
#define VEPU_REG_ENC_CTRL4			0x1ac
#define     VEPU_REG_MV_PENALTY_16X8_8X16(x)		(((x) & 0x3ff) << 20)
#define     VEPU_REG_MV_PENALTY_8X8(x)			(((x) & 0x3ff) << 10)
#define     VEPU_REG_MV_PENALTY_8X4_4X8(x)		(((x) & 0x3ff) << 0)
#define VEPU_REG_ADDR_VP8_PROB_CNT		0x1b0
#define VEPU_REG_INTERRUPT			0x1b4
#define     VEPU_REG_INTERRUPT_NON			BIT(28)
#define     VEPU_REG_MV_WRITE_EN			BIT(24)
#define     VEPU_REG_RECON_WRITE_DIS			BIT(20)
#define     VEPU_REG_INTERRUPT_SLICE_READY_EN		BIT(16)
#define     VEPU_REG_CLK_GATING_EN			BIT(12)
#define     VEPU_REG_INTERRUPT_TIMEOUT_EN		BIT(10)
#define     VEPU_REG_INTERRUPT_RESET			BIT(9)
#define     VEPU_REG_INTERRUPT_DIS_BIT			BIT(8)
#define     VEPU_REG_INTERRUPT_TIMEOUT			BIT(6)
#define     VEPU_REG_INTERRUPT_BUFFER_FULL		BIT(5)
#define     VEPU_REG_INTERRUPT_BUS_ERROR		BIT(4)
#define     VEPU_REG_INTERRUPT_FUSE			BIT(3)
#define     VEPU_REG_INTERRUPT_SLICE_READY		BIT(2)
#define     VEPU_REG_INTERRUPT_FRAME_READY		BIT(1)
#define     VEPU_REG_INTERRUPT_BIT			BIT(0)
#define VEPU_REG_DMV_PENALTY_TBL(i)		(0x1E0 + ((i) * 0x4))
#define     VEPU_REG_DMV_PENALTY_TABLE_BIT(x, i)        ((x) << (i) * 8)
#define VEPU_REG_DMV_Q_PIXEL_PENALTY_TBL(i)	(0x260 + ((i) * 0x4))
#define     VEPU_REG_DMV_Q_PIXEL_PENALTY_TABLE_BIT(x, i)	((x) << (i) * 8)

/* vpu decoder register */
#define VDPU_REG_DEC_CTRL0			0x0c8 // 50
#define     VDPU_REG_REF_BUF_CTRL2_REFBU2_PICID(x)	(((x) & 0x1f) << 25)
#define     VDPU_REG_REF_BUF_CTRL2_REFBU2_THR(x)	(((x) & 0xfff) << 13)
#define     VDPU_REG_CONFIG_TILED_MODE_LSB		BIT(12)
#define     VDPU_REG_CONFIG_DEC_ADV_PRE_DIS		BIT(11)
#define     VDPU_REG_CONFIG_DEC_SCMD_DIS		BIT(10)
#define     VDPU_REG_DEC_CTRL0_SKIP_MODE		BIT(9)
#define     VDPU_REG_DEC_CTRL0_FILTERING_DIS		BIT(8)
#define     VDPU_REG_DEC_CTRL0_PIC_FIXED_QUANT		BIT(7)
#define     VDPU_REG_CONFIG_DEC_LATENCY(x)		(((x) & 0x3f) << 1)
#define     VDPU_REG_CONFIG_TILED_MODE_MSB(x)		BIT(0)
#define     VDPU_REG_CONFIG_DEC_OUT_TILED_E		BIT(0)
#define VDPU_REG_STREAM_LEN			0x0cc
#define     VDPU_REG_DEC_CTRL3_INIT_QP(x)		(((x) & 0x3f) << 25)
#define     VDPU_REG_DEC_STREAM_LEN_HI			BIT(24)
#define     VDPU_REG_DEC_CTRL3_STREAM_LEN(x)		(((x) & 0xffffff) << 0)
#define VDPU_REG_ERROR_CONCEALMENT		0x0d0
#define     VDPU_REG_REF_BUF_CTRL2_APF_THRESHOLD(x)	(((x) & 0x3fff) << 17)
#define     VDPU_REG_ERR_CONC_STARTMB_X(x)		(((x) & 0x1ff) << 8)
#define     VDPU_REG_ERR_CONC_STARTMB_Y(x)		(((x) & 0xff) << 0)
#define VDPU_REG_DEC_FORMAT			0x0d4
#define     VDPU_REG_DEC_CTRL0_DEC_MODE(x)		(((x) & 0xf) << 0)
#define VDPU_REG_DATA_ENDIAN			0x0d8
#define     VDPU_REG_CONFIG_DEC_STRENDIAN_E		BIT(5)
#define     VDPU_REG_CONFIG_DEC_STRSWAP32_E		BIT(4)
#define     VDPU_REG_CONFIG_DEC_OUTSWAP32_E		BIT(3)
#define     VDPU_REG_CONFIG_DEC_INSWAP32_E		BIT(2)
#define     VDPU_REG_CONFIG_DEC_OUT_ENDIAN		BIT(1)
#define     VDPU_REG_CONFIG_DEC_IN_ENDIAN		BIT(0)
#define VDPU_REG_INTERRUPT			0x0dc
#define     VDPU_REG_INTERRUPT_DEC_TIMEOUT		BIT(13)
#define     VDPU_REG_INTERRUPT_DEC_ERROR_INT		BIT(12)
#define     VDPU_REG_INTERRUPT_DEC_PIC_INF		BIT(10)
#define     VDPU_REG_INTERRUPT_DEC_SLICE_INT		BIT(9)
#define     VDPU_REG_INTERRUPT_DEC_ASO_INT		BIT(8)
#define     VDPU_REG_INTERRUPT_DEC_BUFFER_INT		BIT(6)
#define     VDPU_REG_INTERRUPT_DEC_BUS_INT		BIT(5)
#define     VDPU_REG_INTERRUPT_DEC_RDY_INT		BIT(4)
#define     VDPU_REG_INTERRUPT_DEC_IRQ_DIS		BIT(1)
#define     VDPU_REG_INTERRUPT_DEC_IRQ			BIT(0)
#define VDPU_REG_AXI_CTRL			0x0e0
#define     VDPU_REG_AXI_DEC_SEL			BIT(23)
#define     VDPU_REG_CONFIG_DEC_DATA_DISC_E		BIT(22)
#define     VDPU_REG_PARAL_BUS_E(x)			BIT(21)
#define     VDPU_REG_CONFIG_DEC_MAX_BURST(x)		(((x) & 0x1f) << 16)
#define     VDPU_REG_DEC_CTRL0_DEC_AXI_WR_ID(x)		(((x) & 0xff) << 8)
#define     VDPU_REG_CONFIG_DEC_AXI_RD_ID(x)		(((x) & 0xff) << 0)
#define VDPU_REG_EN_FLAGS			0x0e4
#define     VDPU_REG_AHB_HLOCK_E			BIT(31)
#define     VDPU_REG_CACHE_E				BIT(29)
#define     VDPU_REG_PREFETCH_SINGLE_CHANNEL_E		BIT(28)
#define     VDPU_REG_INTRA_3_CYCLE_ENHANCE		BIT(27)
#define     VDPU_REG_INTRA_DOUBLE_SPEED			BIT(26)
#define     VDPU_REG_INTER_DOUBLE_SPEED			BIT(25)
#define     VDPU_REG_DEC_CTRL3_START_CODE_E		BIT(22)
#define     VDPU_REG_DEC_CTRL3_CH_8PIX_ILEAV_E		BIT(21)
#define     VDPU_REG_DEC_CTRL0_RLC_MODE_E		BIT(20)
#define     VDPU_REG_DEC_CTRL0_DIVX3_E			BIT(19)
#define     VDPU_REG_DEC_CTRL0_PJPEG_E			BIT(18)
#define     VDPU_REG_DEC_CTRL0_PIC_INTERLACE_E		BIT(17)
#define     VDPU_REG_DEC_CTRL0_PIC_FIELDMODE_E		BIT(16)
#define     VDPU_REG_DEC_CTRL0_PIC_B_E			BIT(15)
#define     VDPU_REG_DEC_CTRL0_PIC_INTER_E		BIT(14)
#define     VDPU_REG_DEC_CTRL0_PIC_TOPFIELD_E		BIT(13)
#define     VDPU_REG_DEC_CTRL0_FWD_INTERLACE_E		BIT(12)
#define     VDPU_REG_DEC_CTRL0_SORENSON_E		BIT(11)
#define     VDPU_REG_DEC_CTRL0_WRITE_MVS_E		BIT(10)
#define     VDPU_REG_DEC_CTRL0_REF_TOPFIELD_E		BIT(9)
#define     VDPU_REG_DEC_CTRL0_REFTOPFIRST_E		BIT(8)
#define     VDPU_REG_DEC_CTRL0_SEQ_MBAFF_E		BIT(7)
#define     VDPU_REG_DEC_CTRL0_PICORD_COUNT_E		BIT(6)
#define     VDPU_REG_CONFIG_DEC_TIMEOUT_E		BIT(5)
#define     VDPU_REG_CONFIG_DEC_CLK_GATE_E		BIT(4)
#define     VDPU_REG_DEC_CTRL0_DEC_OUT_DIS		BIT(2)
#define     VDPU_REG_REF_BUF_CTRL2_REFBU2_BUF_E		BIT(1)
#define     VDPU_REG_INTERRUPT_DEC_E			BIT(0)
#define VDPU_REG_SOFT_RESET			0x0e8
#define VDPU_REG_PRED_FLT			0x0ec
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_0_0(x)	(((x) & 0x3ff) << 22)
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_0_1(x)	(((x) & 0x3ff) << 12)
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_0_2(x)	(((x) & 0x3ff) << 2)
#define VDPU_REG_ADDITIONAL_CHROMA_ADDRESS	0x0f0
#define VDPU_REG_ADDR_QTABLE			0x0f4
#define VDPU_REG_DIRECT_MV_ADDR			0x0f8
#define VDPU_REG_ADDR_DST			0x0fc
#define VDPU_REG_ADDR_STR			0x100
#define VDPU_REG_REFBUF_RELATED			0x104
#define VDPU_REG_FWD_PIC(i)			(0x128 + ((i) * 0x4))
#define     VDPU_REG_FWD_PIC_PINIT_RLIST_F5(x)		(((x) & 0x1f) << 25)
#define     VDPU_REG_FWD_PIC_PINIT_RLIST_F4(x)		(((x) & 0x1f) << 20)
#define     VDPU_REG_FWD_PIC_PINIT_RLIST_F3(x)		(((x) & 0x1f) << 15)
#define     VDPU_REG_FWD_PIC_PINIT_RLIST_F2(x)		(((x) & 0x1f) << 10)
#define     VDPU_REG_FWD_PIC_PINIT_RLIST_F1(x)		(((x) & 0x1f) << 5)
#define     VDPU_REG_FWD_PIC_PINIT_RLIST_F0(x)		(((x) & 0x1f) << 0)
#define VDPU_REG_REF_PIC(i)			(0x130 + ((i) * 0x4))
#define     VDPU_REG_REF_PIC_REFER1_NBR(x)		(((x) & 0xffff) << 16)
#define     VDPU_REG_REF_PIC_REFER0_NBR(x)		(((x) & 0xffff) << 0)
#define VDPU_REG_H264_ADDR_REF(i)			(0x150 + ((i) * 0x4))
#define     VDPU_REG_ADDR_REF_FIELD_E			BIT(1)
#define     VDPU_REG_ADDR_REF_TOPC_E			BIT(0)
#define VDPU_REG_INITIAL_REF_PIC_LIST0		0x190
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_F5(x)	(((x) & 0x1f) << 25)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_F4(x)	(((x) & 0x1f) << 20)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_F3(x)	(((x) & 0x1f) << 15)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_F2(x)	(((x) & 0x1f) << 10)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_F1(x)	(((x) & 0x1f) << 5)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_F0(x)	(((x) & 0x1f) << 0)
#define VDPU_REG_INITIAL_REF_PIC_LIST1		0x194
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_F11(x)	(((x) & 0x1f) << 25)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_F10(x)	(((x) & 0x1f) << 20)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_F9(x)	(((x) & 0x1f) << 15)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_F8(x)	(((x) & 0x1f) << 10)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_F7(x)	(((x) & 0x1f) << 5)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_F6(x)	(((x) & 0x1f) << 0)
#define VDPU_REG_INITIAL_REF_PIC_LIST2		0x198
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_F15(x)	(((x) & 0x1f) << 15)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_F14(x)	(((x) & 0x1f) << 10)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_F13(x)	(((x) & 0x1f) << 5)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_F12(x)	(((x) & 0x1f) << 0)
#define VDPU_REG_INITIAL_REF_PIC_LIST3		0x19c
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_B5(x)	(((x) & 0x1f) << 25)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_B4(x)	(((x) & 0x1f) << 20)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_B3(x)	(((x) & 0x1f) << 15)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_B2(x)	(((x) & 0x1f) << 10)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_B1(x)	(((x) & 0x1f) << 5)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_B0(x)	(((x) & 0x1f) << 0)
#define VDPU_REG_INITIAL_REF_PIC_LIST4		0x1a0
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_B11(x)	(((x) & 0x1f) << 25)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_B10(x)	(((x) & 0x1f) << 20)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_B9(x)	(((x) & 0x1f) << 15)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_B8(x)	(((x) & 0x1f) << 10)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_B7(x)	(((x) & 0x1f) << 5)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_B6(x)	(((x) & 0x1f) << 0)
#define VDPU_REG_INITIAL_REF_PIC_LIST5		0x1a4
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_B15(x)	(((x) & 0x1f) << 15)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_B14(x)	(((x) & 0x1f) << 10)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_B13(x)	(((x) & 0x1f) << 5)
#define     VDPU_REG_BD_REF_PIC_BINIT_RLIST_B12(x)	(((x) & 0x1f) << 0)
#define VDPU_REG_INITIAL_REF_PIC_LIST6		0x1a8
#define     VDPU_REG_BD_P_REF_PIC_PINIT_RLIST_F3(x)	(((x) & 0x1f) << 15)
#define     VDPU_REG_BD_P_REF_PIC_PINIT_RLIST_F2(x)	(((x) & 0x1f) << 10)
#define     VDPU_REG_BD_P_REF_PIC_PINIT_RLIST_F1(x)	(((x) & 0x1f) << 5)
#define     VDPU_REG_BD_P_REF_PIC_PINIT_RLIST_F0(x)	(((x) & 0x1f) << 0)
#define VDPU_REG_LT_REF				0x1ac
#define VDPU_REG_VALID_REF			0x1b0
#define VDPU_REG_H264_PIC_MB_SIZE		0x1b8
#define     VDPU_REG_DEC_CTRL2_CH_QP_OFFSET2(x)		(((x) & 0x1f) << 22)
#define     VDPU_REG_DEC_CTRL2_CH_QP_OFFSET(x)		(((x) & 0x1f) << 17)
#define     VDPU_REG_DEC_CTRL1_PIC_MB_HEIGHT_P(x)	(((x) & 0xff) << 9)
#define     VDPU_REG_DEC_CTRL1_PIC_MB_WIDTH(x)		(((x) & 0x1ff) << 0)
#define VDPU_REG_H264_CTRL			0x1bc
#define     VDPU_REG_DEC_CTRL4_WEIGHT_BIPR_IDC(x)	(((x) & 0x3) << 16)
#define     VDPU_REG_DEC_CTRL1_REF_FRAMES(x)		(((x) & 0x1f) << 0)
#define VDPU_REG_CURRENT_FRAME			0x1c0
#define     VDPU_REG_DEC_CTRL5_FILT_CTRL_PRES		BIT(31)
#define     VDPU_REG_DEC_CTRL5_RDPIC_CNT_PRES		BIT(30)
#define     VDPU_REG_DEC_CTRL4_FRAMENUM_LEN(x)		(((x) & 0x1f) << 16)
#define     VDPU_REG_DEC_CTRL4_FRAMENUM(x)		(((x) & 0xffff) << 0)
#define VDPU_REG_REF_FRAME			0x1c4
#define     VDPU_REG_DEC_CTRL5_REFPIC_MK_LEN(x)		(((x) & 0x7ff) << 16)
#define     VDPU_REG_DEC_CTRL5_IDR_PIC_ID(x)		(((x) & 0xffff) << 0)
#define VDPU_REG_DEC_CTRL6			0x1c8
#define     VDPU_REG_DEC_CTRL6_PPS_ID(x)		(((x) & 0xff) << 24)
#define     VDPU_REG_DEC_CTRL6_REFIDX1_ACTIVE(x)	(((x) & 0x1f) << 19)
#define     VDPU_REG_DEC_CTRL6_REFIDX0_ACTIVE(x)	(((x) & 0x1f) << 14)
#define     VDPU_REG_DEC_CTRL6_POC_LENGTH(x)		(((x) & 0xff) << 0)
#define VDPU_REG_ENABLE_FLAG			0x1cc
#define     VDPU_REG_DEC_CTRL5_IDR_PIC_E		BIT(8)
#define     VDPU_REG_DEC_CTRL4_DIR_8X8_INFER_E		BIT(7)
#define     VDPU_REG_DEC_CTRL4_BLACKWHITE_E		BIT(6)
#define     VDPU_REG_DEC_CTRL4_CABAC_E			BIT(5)
#define     VDPU_REG_DEC_CTRL4_WEIGHT_PRED_E		BIT(4)
#define     VDPU_REG_DEC_CTRL5_CONST_INTRA_E		BIT(3)
#define     VDPU_REG_DEC_CTRL5_8X8TRANS_FLAG_E		BIT(2)
#define     VDPU_REG_DEC_CTRL2_TYPE1_QUANT_E		BIT(1)
#define     VDPU_REG_DEC_CTRL2_FIELDPIC_FLAG_E		BIT(0)
#define VDPU_REG_VP8_PIC_MB_SIZE		0x1e0
#define     VDPU_REG_DEC_PIC_MB_WIDTH(x)		(((x) & 0x1ff) << 23)
#define	    VDPU_REG_DEC_MB_WIDTH_OFF(x)		(((x) & 0xf) << 19)
#define	    VDPU_REG_DEC_PIC_MB_HEIGHT_P(x)		(((x) & 0xff) << 11)
#define     VDPU_REG_DEC_MB_HEIGHT_OFF(x)		(((x) & 0xf) << 7)
#define     VDPU_REG_DEC_CTRL1_PIC_MB_W_EXT(x)		(((x) & 0x7) << 3)
#define     VDPU_REG_DEC_CTRL1_PIC_MB_H_EXT(x)		(((x) & 0x7) << 0)
#define VDPU_REG_VP8_DCT_START_BIT		0x1e4
#define     VDPU_REG_DEC_CTRL4_DCT1_START_BIT(x)	(((x) & 0x3f) << 26)
#define     VDPU_REG_DEC_CTRL4_DCT2_START_BIT(x)	(((x) & 0x3f) << 20)
#define     VDPU_REG_DEC_CTRL4_VC1_HEIGHT_EXT		BIT(13)
#define     VDPU_REG_DEC_CTRL4_BILIN_MC_E		BIT(12)
#define VDPU_REG_VP8_CTRL0			0x1e8
#define     VDPU_REG_DEC_CTRL2_STRM_START_BIT(x)	(((x) & 0x3f) << 26)
#define     VDPU_REG_DEC_CTRL2_STRM1_START_BIT(x)	(((x) & 0x3f) << 18)
#define     VDPU_REG_DEC_CTRL2_BOOLEAN_VALUE(x)		(((x) & 0xff) << 8)
#define     VDPU_REG_DEC_CTRL2_BOOLEAN_RANGE(x)		(((x) & 0xff) << 0)
#define VDPU_REG_VP8_DATA_VAL			0x1f0
#define     VDPU_REG_DEC_CTRL6_COEFFS_PART_AM(x)	(((x) & 0xf) << 24)
#define     VDPU_REG_DEC_CTRL6_STREAM1_LEN(x)		(((x) & 0xffffff) << 0)
#define VDPU_REG_PRED_FLT7			0x1f4
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_5_1(x)	(((x) & 0x3ff) << 22)
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_5_2(x)	(((x) & 0x3ff) << 12)
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_5_3(x)	(((x) & 0x3ff) << 2)
#define VDPU_REG_PRED_FLT8			0x1f8
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_6_0(x)	(((x) & 0x3ff) << 22)
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_6_1(x)	(((x) & 0x3ff) << 12)
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_6_2(x)	(((x) & 0x3ff) << 2)
#define VDPU_REG_PRED_FLT9			0x1fc
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_6_3(x)	(((x) & 0x3ff) << 22)
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_7_0(x)	(((x) & 0x3ff) << 12)
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_7_1(x)	(((x) & 0x3ff) << 2)
#define VDPU_REG_PRED_FLT10			0x200
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_7_2(x)	(((x) & 0x3ff) << 22)
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_7_3(x)	(((x) & 0x3ff) << 12)
#define     VDPU_REG_BD_REF_PIC_PRED_TAP_2_M1(x)	(((x) & 0x3) << 10)
#define     VDPU_REG_BD_REF_PIC_PRED_TAP_2_4(x)		(((x) & 0x3) << 8)
#define     VDPU_REG_BD_REF_PIC_PRED_TAP_4_M1(x)	(((x) & 0x3) << 6)
#define     VDPU_REG_BD_REF_PIC_PRED_TAP_4_4(x)		(((x) & 0x3) << 4)
#define     VDPU_REG_BD_REF_PIC_PRED_TAP_6_M1(x)	(((x) & 0x3) << 2)
#define     VDPU_REG_BD_REF_PIC_PRED_TAP_6_4(x)		(((x) & 0x3) << 0)
#define VDPU_REG_FILTER_LEVEL			0x204
#define     VDPU_REG_REF_PIC_LF_LEVEL_0(x)		(((x) & 0x3f) << 18)
#define     VDPU_REG_REF_PIC_LF_LEVEL_1(x)		(((x) & 0x3f) << 12)
#define     VDPU_REG_REF_PIC_LF_LEVEL_2(x)		(((x) & 0x3f) << 6)
#define     VDPU_REG_REF_PIC_LF_LEVEL_3(x)		(((x) & 0x3f) << 0)
#define VDPU_REG_VP8_QUANTER0			0x208
#define     VDPU_REG_REF_PIC_QUANT_DELTA_0(x)		(((x) & 0x1f) << 27)
#define     VDPU_REG_REF_PIC_QUANT_DELTA_1(x)		(((x) & 0x1f) << 22)
#define     VDPU_REG_REF_PIC_QUANT_0(x)			(((x) & 0x7ff) << 11)
#define     VDPU_REG_REF_PIC_QUANT_1(x)			(((x) & 0x7ff) << 0)
#define VDPU_REG_VP8_ADDR_REF0			0x20c
#define VDPU_REG_FILTER_MB_ADJ			0x210
#define     VDPU_REG_REF_PIC_FILT_TYPE_E		BIT(31)
#define     VDPU_REG_REF_PIC_FILT_SHARPNESS(x)		(((x) & 0x7) << 28)
#define     VDPU_REG_FILT_MB_ADJ_0(x)			(((x) & 0x7f) << 21)
#define     VDPU_REG_FILT_MB_ADJ_1(x)			(((x) & 0x7f) << 14)
#define     VDPU_REG_FILT_MB_ADJ_2(x)			(((x) & 0x7f) << 7)
#define     VDPU_REG_FILT_MB_ADJ_3(x)			(((x) & 0x7f) << 0)
#define VDPU_REG_FILTER_REF_ADJ			0x214
#define     VDPU_REG_REF_PIC_ADJ_0(x)			(((x) & 0x7f) << 21)
#define     VDPU_REG_REF_PIC_ADJ_1(x)			(((x) & 0x7f) << 14)
#define     VDPU_REG_REF_PIC_ADJ_2(x)			(((x) & 0x7f) << 7)
#define     VDPU_REG_REF_PIC_ADJ_3(x)			(((x) & 0x7f) << 0)
#define VDPU_REG_VP8_ADDR_REF2_5(i)		(0x218 + ((i) * 0x4))
#define     VDPU_REG_VP8_GREF_SIGN_BIAS			BIT(0)
#define     VDPU_REG_VP8_AREF_SIGN_BIAS			BIT(0)
#define VDPU_REG_VP8_DCT_BASE(i)		(0x230 + ((i) * 0x4))
#define VDPU_REG_VP8_ADDR_CTRL_PART		0x244
#define VDPU_REG_VP8_ADDR_REF1			0x250
#define VDPU_REG_VP8_SEGMENT_VAL		0x254
#define     VDPU_REG_FWD_PIC1_SEGMENT_BASE(x)		((x) << 0)
#define     VDPU_REG_FWD_PIC1_SEGMENT_UPD_E		BIT(1)
#define     VDPU_REG_FWD_PIC1_SEGMENT_E			BIT(0)
#define VDPU_REG_VP8_DCT_START_BIT2		0x258
#define     VDPU_REG_DEC_CTRL7_DCT3_START_BIT(x)	(((x) & 0x3f) << 24)
#define     VDPU_REG_DEC_CTRL7_DCT4_START_BIT(x)	(((x) & 0x3f) << 18)
#define     VDPU_REG_DEC_CTRL7_DCT5_START_BIT(x)	(((x) & 0x3f) << 12)
#define     VDPU_REG_DEC_CTRL7_DCT6_START_BIT(x)	(((x) & 0x3f) << 6)
#define     VDPU_REG_DEC_CTRL7_DCT7_START_BIT(x)	(((x) & 0x3f) << 0)
#define VDPU_REG_VP8_QUANTER1			0x25c
#define     VDPU_REG_REF_PIC_QUANT_DELTA_2(x)		(((x) & 0x1f) << 27)
#define     VDPU_REG_REF_PIC_QUANT_DELTA_3(x)		(((x) & 0x1f) << 22)
#define     VDPU_REG_REF_PIC_QUANT_2(x)			(((x) & 0x7ff) << 11)
#define     VDPU_REG_REF_PIC_QUANT_3(x)			(((x) & 0x7ff) << 0)
#define VDPU_REG_VP8_QUANTER2			0x260
#define     VDPU_REG_REF_PIC_QUANT_DELTA_4(x)		(((x) & 0x1f) << 27)
#define     VDPU_REG_REF_PIC_QUANT_4(x)			(((x) & 0x7ff) << 11)
#define     VDPU_REG_REF_PIC_QUANT_5(x)			(((x) & 0x7ff) << 0)
#define VDPU_REG_PRED_FLT1			0x264
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_0_3(x)	(((x) & 0x3ff) << 22)
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_1_0(x)	(((x) & 0x3ff) << 12)
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_1_1(x)	(((x) & 0x3ff) << 2)
#define VDPU_REG_PRED_FLT2			0x268
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_1_2(x)	(((x) & 0x3ff) << 22)
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_1_3(x)	(((x) & 0x3ff) << 12)
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_2_0(x)	(((x) & 0x3ff) << 2)
#define VDPU_REG_PRED_FLT3			0x26c
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_2_1(x)	(((x) & 0x3ff) << 22)
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_2_2(x)	(((x) & 0x3ff) << 12)
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_2_3(x)	(((x) & 0x3ff) << 2)
#define VDPU_REG_PRED_FLT4			0x270
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_3_0(x)	(((x) & 0x3ff) << 22)
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_3_1(x)	(((x) & 0x3ff) << 12)
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_3_2(x)	(((x) & 0x3ff) << 2)
#define VDPU_REG_PRED_FLT5			0x274
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_3_3(x)	(((x) & 0x3ff) << 22)
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_4_0(x)	(((x) & 0x3ff) << 12)
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_4_1(x)	(((x) & 0x3ff) << 2)
#define VDPU_REG_PRED_FLT6			0x278
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_4_2(x)	(((x) & 0x3ff) << 22)
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_4_3(x)	(((x) & 0x3ff) << 12)
#define     VDPU_REG_PRED_FLT_PRED_BC_TAP_5_0(x)	(((x) & 0x3ff) << 2)

#endif /* RK3399_VPU_REGS_H_ */

/*
 * Rockchip RK3288 VPU codec driver
 *
 * Copyright (C) 2014 Google, Inc.
 *	Tomasz Figa <tfiga@chromium.org>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef RK3288_VPU_REGS_H_
#define RK3288_VPU_REGS_H_

/* Encoder registers. */
#define VEPU_REG_INTERRUPT			0x004
#define     VEPU_REG_INTERRUPT_DIS_BIT		BIT(1)
#define     VEPU_REG_INTERRUPT_BIT		BIT(0)
#define VEPU_REG_AXI_CTRL			0x008
#define     VEPU_REG_AXI_CTRL_OUTPUT_SWAP16	BIT(15)
#define     VEPU_REG_AXI_CTRL_INPUT_SWAP16	BIT(14)
#define     VEPU_REG_AXI_CTRL_BURST_LEN(x)	((x) << 8)
#define     VEPU_REG_AXI_CTRL_GATE_BIT		BIT(4)
#define     VEPU_REG_AXI_CTRL_OUTPUT_SWAP32	BIT(3)
#define     VEPU_REG_AXI_CTRL_INPUT_SWAP32	BIT(2)
#define     VEPU_REG_AXI_CTRL_OUTPUT_SWAP8	BIT(1)
#define     VEPU_REG_AXI_CTRL_INPUT_SWAP8	BIT(0)
#define VEPU_REG_ADDR_OUTPUT_STREAM		0x014
#define VEPU_REG_ADDR_OUTPUT_CTRL		0x018
#define VEPU_REG_ADDR_REF_LUMA			0x01c
#define VEPU_REG_ADDR_REF_CHROMA		0x020
#define VEPU_REG_ADDR_REC_LUMA			0x024
#define VEPU_REG_ADDR_REC_CHROMA		0x028
#define VEPU_REG_ADDR_IN_LUMA			0x02c
#define VEPU_REG_ADDR_IN_CB			0x030
#define VEPU_REG_ADDR_IN_CR			0x034
#define VEPU_REG_ENC_CTRL			0x038
#define     VEPU_REG_ENC_CTRL_NAL_MODE_BIT	BIT(29)
#define     VEPU_REG_ENC_CTRL_WIDTH(w)		((w) << 19)
#define     VEPU_REG_ENC_CTRL_HEIGHT(h)		((h) << 10)
#define     VEPU_REG_ENC_CTRL_KEYFRAME_BIT	BIT(3)
#define     VEPU_REG_ENC_CTRL_ENC_MODE_VP8	(0x1 << 1)
#define     VEPU_REG_ENC_CTRL_EN_BIT		BIT(0)
#define VEPU_REG_IN_IMG_CTRL			0x03c
#define     VEPU_REG_IN_IMG_CTRL_ROW_LEN(x)	((x) << 12)
#define     VEPU_REG_IN_IMG_CTRL_OVRFLR_D4(x)	((x) << 10)
#define     VEPU_REG_IN_IMG_CTRL_OVRFLB_D4(x)	((x) << 6)
#define     VEPU_REG_IN_IMG_CTRL_FMT(x)		((x) << 2)
#define VEPU_REG_ENC_CTRL0			0x040
#define VEPU_REG_ENC_CTRL1			0x044
#define VEPU_REG_ENC_CTRL2			0x048
#define VEPU_REG_ENC_CTRL3			0x04c
#define VEPU_REG_ENC_CTRL5			0x050
#define VEPU_REG_ENC_CTRL4			0x054
#define VEPU_REG_STR_HDR_REM_MSB		0x058
#define VEPU_REG_STR_HDR_REM_LSB		0x05c
#define VEPU_REG_STR_BUF_LIMIT			0x060
#define VEPU_REG_MAD_CTRL			0x064
#define VEPU_REG_ADDR_VP8_PROB_CNT		0x068
#define VEPU_REG_QP_VAL				0x06c
#define VEPU_REG_VP8_QP_VAL(i)			(0x06c + ((i) * 0x4))
#define VEPU_REG_CHECKPOINT(i)			(0x070 + ((i) * 0x4))
#define VEPU_REG_CHKPT_WORD_ERR(i)		(0x084 + ((i) * 0x4))
#define VEPU_REG_VP8_BOOL_ENC			0x08c
#define VEPU_REG_CHKPT_DELTA_QP			0x090
#define VEPU_REG_VP8_CTRL0			0x090
#define VEPU_REG_RLC_CTRL			0x094
#define     VEPU_REG_RLC_CTRL_STR_OFFS_SHIFT	23
#define     VEPU_REG_RLC_CTRL_STR_OFFS_MASK	(0x3f << 23)
#define VEPU_REG_MB_CTRL			0x098
#define VEPU_REG_ADDR_CABAC_TBL			0x0cc
#define VEPU_REG_ADDR_MV_OUT			0x0d0
#define VEPU_REG_RGB_YUV_COEFF(i)		(0x0d4 + ((i) * 0x4))
#define VEPU_REG_RGB_MASK_MSB			0x0dc
#define VEPU_REG_INTRA_AREA_CTRL		0x0e0
#define VEPU_REG_CIR_INTRA_CTRL			0x0e4
#define VEPU_REG_INTRA_SLICE_BITMAP(i)		(0x0e8 + ((i) * 0x4))
#define VEPU_REG_ADDR_VP8_DCT_PART(i)		(0x0e8 + ((i) * 0x4))
#define VEPU_REG_FIRST_ROI_AREA			0x0f0
#define VEPU_REG_SECOND_ROI_AREA		0x0f4
#define VEPU_REG_MVC_CTRL			0x0f8
#define VEPU_REG_VP8_INTRA_PENALTY(i)		(0x100 + ((i) * 0x4))
#define VEPU_REG_ADDR_VP8_SEG_MAP		0x11c
#define VEPU_REG_VP8_SEG_QP(i)			(0x120 + ((i) * 0x4))
#define VEPU_REG_DMV_4P_1P_PENALTY(i)		(0x180 + ((i) * 0x4))
#define VEPU_REG_DMV_QPEL_PENALTY(i)		(0x200 + ((i) * 0x4))
#define VEPU_REG_VP8_CTRL1			0x280
#define VEPU_REG_VP8_BIT_COST_GOLDEN		0x284
#define VEPU_REG_VP8_LOOP_FLT_DELTA(i)		(0x288 + ((i) * 0x4))

#endif /* RK3288_VPU_REGS_H_ */
